import os
import sys
import numpy as np
from scipy.stats import wilcoxon

import firebase_admin
from firebase_admin import credentials
from firebase_admin import db

DIR = os.path.dirname(os.path.abspath(__file__))

# direktorija LSTM in SARIMA moram dodati na seznam map, ki jih python uporabi za iskanje importov
sys.path.append(os.path.join(DIR, "LSTM"))
sys.path.append(os.path.join(DIR, "SARIMA"))
from LSTM import myNeuralNet
from SARIMA import sarimaDailySeasonality as sarima

# pot do datoteke s kljucem do firebaseove oblacne baze
KEY_FILENAME = "firebase/firebase-admin_private_key.json"
KEY_PATH = os.path.join(DIR, KEY_FILENAME)
from secret import DATABASE_URL

# zagon clienta od firebase
cred = credentials.Certificate(KEY_PATH)
firebase_admin.initialize_app(cred, {
    "databaseURL": DATABASE_URL
})
# referenca do baze
DATABASE_PATH_MEASURMENTS = "/measurments"
ref = db.reference(DATABASE_PATH_MEASURMENTS)

def testForecasts():
    numOfMeasurmentsFromDB = 32 * 24 # dni meritev [v urah]

    # stevilo meritev in ustvarim numpy polje
    numOfModels = 6
    numOfMeasurmentsLSTM = 5 * 24
    numOfMeasurmentsSARIMA = 10 * 24
    numOfVariables = 1
    readings = np.empty((numOfMeasurmentsFromDB, numOfVariables), np.float)

    # pridobim "snapshot" zadnjih-numOfMeasurments meritev
    snapshot = ref.order_by_key().start_at("2021-07-10T01:00").limit_to_first(numOfMeasurmentsFromDB).get()

    # loop cez pridobljene poizvedbe, shranim v polja
    samplesLen = 0
    for key_ISOdate, subJSON in snapshot.items():
        temperature = subJSON["temperature"]
        humidity = subJSON["humidity"]

        readings[samplesLen, 0] = temperature
        if numOfVariables == 2:
            readings[samplesLen, 1] = humidity
        samplesLen += 1

        print(key_ISOdate)

    print("\n\n\n-------------------------------------LSTM--------------------------------------------\n\n")
    inputLSTMoffset = numOfMeasurmentsLSTM # zamik vhoda, podobno kot spodnji komentar
    indexfirstForecastStart = numOfMeasurmentsSARIMA # samo zato, da se bojo datumi napovedi ujemali s SARIMA (ki potrebuje več preteklih meritev)
    indexLastForecastStart = len(readings) - numOfMeasurmentsLSTM
    errorsLSTM = np.zeros(((indexLastForecastStart + 1) - indexfirstForecastStart, numOfModels), np.float)
    modelTestDataStartIndexes = [0, 23, 47, 71, 95, 119]
    end = len(readings)
    modelTestDataEndIndexes = [end - 119, end - 96,  end - 72, end - 48, end - 24, end - 0]

    for i in range(numOfModels):
        lstm = myNeuralNet.NeuralNet(indexOfFutureSampleListToPredict=i, loadModel=True)
        y_pred = lstm.generatePrediction(input=readings[inputLSTMoffset : indexLastForecastStart], hoursPast=numOfMeasurmentsLSTM)
        y = readings[indexfirstForecastStart + modelTestDataStartIndexes[i] : modelTestDataEndIndexes[i]]

        print("Metrike testne množice - model", i, ":")
        print(y.shape, y_pred.shape)

        y = np.reshape(y, y.size)
        errors = y - y_pred

        mae = np.mean(np.abs(errors))
        print("MAE:", mae)

        mse = np.mean(errors**2)
        print("MSE:", mse)

        avgError = np.mean(errors)
        stdDeviation = np.sqrt(np.mean((errors - avgError)**2))
        print("Standardni odklon:", stdDeviation, "\n")

        errorsLSTM[ : , i] = errors

        #lstm.plotPredictionVsRealData(y, y_pred)

    #napoved sarima
    print("\n\n\n-------------------------------------SARIMA--------------------------------------------\n\n")
    errorsSARIMA = np.zeros(((indexLastForecastStart + 1) - indexfirstForecastStart, numOfModels), np.float)
    importantIndexes = [0, 23, 47, 71, 95, 119]

    for i in range(len(errorsSARIMA)):
        input = readings[i : i + indexfirstForecastStart, 0]

        # prilagajanje 1 (rocni, fiksni parametri)
        y = readings[i + indexfirstForecastStart : i + numOfMeasurmentsSARIMA + numOfMeasurmentsLSTM, 0]
        y_pred = sarima.generatePredictionFromData(input)

        """
        # prilagajanje 2 (avtomatski parametri)
        model = pm.auto_arima(input, start_p=0, start_q=0, max_P=3, max_Q=3,
                           max_p=3, max_q=3, m=24,
                           start_P=0, start_Q=0, seasonal=True,
                           d=0, D=1, trace=True, 
                           stepwise=True)
        y_pred = model.predict(n_periods=5*24)
        """

        #sarima.plotGraph("Časovna vrsta", input, "Temperatura [°C]", "vzorec (v urah)", "temperatura")

        print(y.shape, y_pred.shape)
        predictionLineErrors = y - y_pred

        for j in range(len(importantIndexes)):
            errorsSARIMA[i, j] = predictionLineErrors[importantIndexes[j]]

        #sarima.plotGraph("Napoved", y, "dejanske vrednosti", "Zaporedje ur", "Temperatura [°C]", y_pred, "napovedi")

    for j in range(len(importantIndexes)):
        errorsAtSelectedTime = errorsSARIMA[ : , j]

        mae = np.mean(np.abs(errorsAtSelectedTime))
        print("MAE_" + str(j) + ": " + str(mae))

        mse = np.mean(errorsAtSelectedTime**2)
        print("MSE_" + str(j) + ": " + str(mse))

        avgError = np.mean(errorsAtSelectedTime)
        stdDeviation = np.sqrt(np.mean((errorsAtSelectedTime - avgError)**2))
        print("Standardni odklon_" + str(j) + ": " + str(stdDeviation) + "\n")

    print("\n\n\n----------------------------------- Neparametrični parni test Wilcoxon --------------------------------------------\n\n")
    alpha = 0.05 # stopnja znacilnosti (pomeni 95% stopnjo zaupanja)

    for i in range(numOfModels):
        w, p = wilcoxon(errorsLSTM[ : , i], errorsSARIMA[ : , i])
        print("Primerjava metod za model stevilka", i)

        if p < alpha:
            print("Hipoteza H0 je zavrnjena, s tem trdimo da metodi dajeta različne rezultate, p vrednost je:", p)
        else:
            print("Hipoteza H0 ni zavrnjena (porazdelitvi napak obeh metod sta isti / odvisni od nakljucja), p vrednost je:", p, "\n")

if __name__ == "__main__":
    testForecasts()