import pandas as pd
import numpy as np
from tensorflow.keras import layers, Model, models, optimizers, callbacks, backend as K
from sklearn.preprocessing import MinMaxScaler
import joblib
from matplotlib import pyplot as plt

from myGlobals import DATA_SAMPLES, SCALER_TEMPERATURE, SCALER_TIME, HISTORY, MODEL, joinFileExtension, NUM_SAMPLES_PAST, TRAIN_RATIO, NUM_OF_INPUT_VARIABLES, SAMPLES_IN_FUTURE_LIST, NUM_OF_MODELS

class NeuralNet:
    # (private) metoda, ki definira custom merilo natacnosti mreze
    def soft_acc(self, y_true, y_pred):
        return K.mean(K.equal(K.round(y_true), K.round(y_pred)))

    # konstruktor (indexOfFutureSampleToPredict doda izhodnim datotekam se indeks)
    def __init__(self, indexOfFutureSampleListToPredict=-1, loadModel=False):
        self.index = indexOfFutureSampleListToPredict
        if self.index > -1 and self.index < NUM_OF_MODELS:
            self.futureSampleToPredict = SAMPLES_IN_FUTURE_LIST[self.index]
        else:
            self.futureSampleToPredict = -1

        if loadModel and self.futureSampleToPredict != -1:
            self.history = pd.read_csv(joinFileExtension(HISTORY, self.index))
            self.scalerTemperature = joblib.load(joinFileExtension(SCALER_TEMPERATURE, self.index))
            if NUM_OF_INPUT_VARIABLES == 2:
                self.scalerTime = joblib.load(joinFileExtension(SCALER_TIME, self.index))
            self.model = models.load_model(joinFileExtension(MODEL, self.index), custom_objects={"soft_acc": self.soft_acc})
        else:
            self.history = None
            self.scalerTemperature = None
            if NUM_OF_INPUT_VARIABLES == 2:
                self.scalerTime = None
            self.model = None

    # metoda, ki generira casovno zapordje vzorcev
    def timeseriesGeneration(self, arr):
        # [st. napovedi, hoursPast, numOfVariables]
        series = np.empty((np.size(arr, axis=0) - (NUM_SAMPLES_PAST - 1), NUM_SAMPLES_PAST, arr[0].size))

        for i in range(np.size(series, axis=0)):
            series[i, : ] = arr[i : i + NUM_SAMPLES_PAST]

        return series
    
    # metoda, ki prebere podatke iz csv
    def readFromDatasamples(self):
        df = pd.read_csv(DATA_SAMPLES)

        if NUM_OF_INPUT_VARIABLES == 1:
            data = df[["T [°C]"]].values[::2]
        elif NUM_OF_INPUT_VARIABLES == 2:
            #data = df[["T [°C]","H [%]"]].values[::2]

            data = df[["T [°C]","date"]].values[::2]

            # pridobi samo uro iz datuma
            for row in data:
                date = row[1].split(" ")[1]
                row[1] = float(date[0 : 2])

            # ker so različni tipi podatkov nastane numpy polje tipa "object", spremenim v float
            data = data.astype(np.float)
        
        return data

    # metoda za normalizacijo in standardizacijo, shrani se scaler
    def scaleData(self, data):
        filesAlreadyExist = True
        
        if self.scalerTemperature is None:
            self.scalerTemperature = MinMaxScaler(feature_range=(0, 1), copy=False)
            filesAlreadyExist = False

        data[:, 0] = self.scalerTemperature.fit_transform(data[:, 0].reshape(-1, 1)).reshape((np.size(data, axis=0)))
        
        if not filesAlreadyExist:
            joblib.dump(self.scalerTemperature, joinFileExtension(SCALER_TEMPERATURE, self.index))

        if NUM_OF_INPUT_VARIABLES == 2:
            if self.scalerTime is None:
                self.scalerTime = MinMaxScaler(feature_range=(0, 1), copy=False)

            data[:, 1] = self.scalerTime.fit_transform(data[:, 1].reshape(-1, 1)).reshape((np.size(data, axis=0)))

            if not filesAlreadyExist:
                joblib.dump(self.scalerTime, joinFileExtension(SCALER_TIME, self.index))

        print("Podatki normalizirani in standardizirani")
        return data

    # metoda, ki razdeli na ucno in testno mnozico
    def splitTrainTest(self, data):
        # razdelitev na ucno in testno mnozico
        TRAIN_TEST_INDEX = round(np.size(data, axis=0) * TRAIN_RATIO)
        train = data[0 : TRAIN_TEST_INDEX]
        test = data[TRAIN_TEST_INDEX : ]

        # vhodni(x) mnozici train in test (zadnjih futureSampleToPredict vzorcev je ze napoved)
        x_train = train[0 : -self.futureSampleToPredict]
        x_test = test[0 : -self.futureSampleToPredict]

        # pricakovani izhodni(y) mnozici train in test (prvih START_INDEX_Y vzorcev ne gre napovedati)
        START_INDEX_Y = NUM_SAMPLES_PAST + self.futureSampleToPredict - 1
        y_train = np.copy(train[START_INDEX_Y : , 0])
        y_test = np.copy(test[START_INDEX_Y : , 0])

        print("Razdelitev na ucno in testno mnozico v razmerju:", y_train.size, "-", y_test.size)
        return x_train, x_test, y_train, y_test

    # metoda, ki pripravi podatke za ucenje
    def prepareTrainDataFromDatasamples(self):
        if self.futureSampleToPredict == -1:
            print("Napacno podan indeks napovedi v prihodnost (indexOfFutureSampleListToPredict), priprava podatkov prekinjena...")
            return None

        data = self.readFromDatasamples()

        data = self.scaleData(data)

        x_train, x_test, y_train, y_test = self.splitTrainTest(data)

        x_train = self.timeseriesGeneration(x_train)
        x_test = self.timeseriesGeneration(x_test)

        print("Oblike ucnih in testnih mnozic z drsecim oknom:", x_train.shape , "-", x_test.shape)
        return x_train, x_test, y_train, y_test

    # metoda, ki pripravi testno (vhodno ter izhodno) mnozico
    def splitTestOnly(self, data):
        # testna mnozica
        TRAIN_TEST_INDEX = round(np.size(data, axis=0) * TRAIN_RATIO)
        test = data[TRAIN_TEST_INDEX : ]

        # vhodna(x) mnozica train (zadnjih futureSampleToPredict vzorcev je ze napoved)
        x_test = test[0 : -self.futureSampleToPredict]

        # pricakovani izhodni(y) mnozici train in test (prvih START_INDEX_Y vzorcev ne gre napovedati)
        START_INDEX_Y = NUM_SAMPLES_PAST + self.futureSampleToPredict - 1
        y_test = np.copy(test[START_INDEX_Y : , 0])

        return x_test, y_test

    # metoda, ki pripravi podatke za testiranje napovedi iz datasamplov
    def prepareTestDataFromDatasamples(self):
        if self.futureSampleToPredict == -1:
            print("Napacno podan indeks napovedi v prihodnost (indexOfFutureSampleListToPredict), priprava podatkov prekinjena...")
            return None

        data = self.readFromDatasamples()

        x_test, y_test = self.splitTestOnly(data)

        return x_test, y_test

    # metoda, ki nauci model
    def train(self):
        if self.futureSampleToPredict == -1:
            print("Napacno podan indeks napovedi v prihodnost (indexOfFutureSampleListToPredict), ucenje prekinjeno...\n")
            return

        # priprava podatkov v ustrezno obliko
        x_train, x_test, y_train, y_test = self.prepareTrainDataFromDatasamples()

        # arhitektura mreze
        inputLayer = layers.Input(shape=(NUM_SAMPLES_PAST, x_test[0, 0].size))
        lstm_layer = layers.LSTM(units=40)(inputLayer)
        outputLayer = layers.Dense(units=1)(lstm_layer)

        self.model = Model(inputs=inputLayer, outputs=outputLayer)
        self.model.summary()
        self.model.compile(
            loss=["mse"],
            optimizer=optimizers.Adam(learning_rate=0.001),
            metrics=["mse", "mae", self.soft_acc]
        )

        # "callback" funkcija za predcasno prenehanje ucenja
        earlyCallback = callbacks.EarlyStopping(
            monitor="val_loss",
            min_delta=0,
            patience=3,
            verbose=1
        )

        # ucenje
        self.history = self.model.fit(
            x=x_train,
            y=y_train,
            validation_data=(x_test, y_test),
            validation_batch_size=128,
            epochs=18,
            verbose=1,
            callbacks=[earlyCallback]
        ).history

        # shrani metrike in model
        history_df = pd.DataFrame(self.history)
        history_df.to_csv(joinFileExtension(HISTORY, self.index), index=False)
        self.model.save(joinFileExtension(MODEL, self.index))
        print("\n----------------------------------------\nModel naucen in shranjen!\n----------------------------------------")
    
    # metoda, ki generira napoved (eno ali polje napovedi, mozno vec korakov vnaprej)
    def generatePrediction(self, input, hoursPast=NUM_SAMPLES_PAST, steps=1, dataAlreadyScaled=False):
        if self.model is None:
            print("Model ni naucen/nalozen, napoved ni mogoca....\n")
            return

        # skaliranje vhoda
        if not dataAlreadyScaled:
            input = self.scaleData(np.copy(input))

        # spravi v zapis casovne vrste
        x = self.timeseriesGeneration(input)

        if steps == 1:
            # napoved samo stopnjo naprej
            prediction = self.model.predict(x, verbose=1)

        else:
            # ---"tekoca" napoved---
            # polje nadaljnih vhodov
            x_rolling = np.empty(((1, hoursPast + steps)), dtype=np.float)
            start = 0
            stop = hoursPast
            x_rolling[:, start : stop] = x.reshape(1, -1)

            for i in range(steps):
                # napoved
                nextPrediciton = self.model.predict(x_rolling[:, start : stop], verbose=1)

                # dodaj zadnji izhod kot nov vhod
                start += 1
                stop += 1
                x_rolling[:, stop-1] = nextPrediciton

            # polje samo napovedi
            prediction = x_rolling[:, hoursPast : ]

        # inverzno skaliranje
        y_pred = self.scalerTemperature.inverse_transform(prediction.reshape(-1, 1))

        return y_pred.reshape(-1)
    
    # metoda, ki narise graf z matplotlib
    def plotGraph(self, title, x, y, xName, yName, label, color1="r", color2="b", y2=None, label2=None):
        plt.figure(figsize=(10,4))
        plt.plot(x, y, color1, label=label)
        if y2 is not None:
            plt.plot(x, y2, color2, label=label2)
        plt.title(title)
        plt.xlabel(xName)
        plt.ylabel(yName)
        plt.legend(loc="upper left", fontsize=14)
        plt.show()

    # metoda za graf metrik
    def plotTrainingAndValidationMetrics(self):
        if self.history is None:
            print("Model se ni naucen / zgodovina ni prebrana, zato izris metrik ni mogoc...\n")

        else:
            lossArr = self.history["loss"]
            lossValArr = self.history["val_loss"]
            accuracyValArr = self.history["val_soft_acc"]
            epochArr = range(len(lossArr))

            self.plotGraph("Izguba (MSE) modela", epochArr, lossArr, "Epohe", "MSE", "Ucenje", y2=lossValArr, label2="Validacija")
            self.plotGraph("Natancost modela", epochArr, accuracyValArr, "Epohe", "Natancnost", "Validacija")

    # metoda za graf primerjave napovedanih vrednosti in dejanskih
    def plotPredictionVsRealData(self, trueData, predictedData, start=0, end=0):
        if start <= 0 or start >= end:
            start = 0
        if end <= 0 or end > trueData.size:
            end = trueData.size

        xAxis = range(len(trueData))

        self.plotGraph("Primerjava dejanskih in napovedanih vrednosti", xAxis[start : end], trueData[start : end], "Zaporedje ur", "Temperatura [°C]", "Dejanske vrednosti", color2="g", y2=predictedData[start : end], label2="Napoved")

if __name__ == "__main__":
    print("\n\n\n")

    """# ucenje
    for index in range(NUM_OF_MODELS):
        obj = NeuralNet(index)
        obj.train()
        obj.plotTrainingAndValidationMetrics()"""

    # test
    for index in range(NUM_OF_MODELS):
        obj = NeuralNet(index, loadModel=True)

        x, y = obj.prepareTestDataFromDatasamples()

        toFuture = 24 * 5

        y_pred = obj.generatePrediction(x, steps=1)
        if index == 0:
            y_pred_rolling = obj.generatePrediction(x[0 : NUM_SAMPLES_PAST], steps=toFuture)

        obj.plotTrainingAndValidationMetrics()
        obj.plotPredictionVsRealData(y, y_pred, start=0, end=0)
        if index == 0:
            obj.plotPredictionVsRealData(y[0 : toFuture], y_pred_rolling)
        print(y_pred_rolling)
        print("Metrike testne množice - model", index, ":")
        print(y.shape, y_pred.shape)
        errors = y - y_pred

        mae = np.mean(np.abs(errors))
        print("MAE:", mae)

        mse = np.mean(errors**2)
        print("MSE:", mse)

        avgError = np.mean(errors)
        stdDeviation = np.sqrt(np.mean((errors - avgError)**2))
        print("Standardni odklon:", stdDeviation)