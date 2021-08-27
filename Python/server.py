import os
import sys
import numpy as np
import json
from datetime import datetime
import locale

import firebase_admin
from firebase_admin import credentials
from firebase_admin import db

from flask import Flask, render_template

DIR = os.path.dirname(os.path.abspath(__file__))

# direktorija LSTM in SARIMA moram dodati na seznam map, ki jih python uporabi za iskanje importov
sys.path.append(os.path.join(DIR, "LSTM"))
sys.path.append(os.path.join(DIR, "SARIMA"))
from LSTM import myNeuralNet
from SARIMA import sarimaDailySeasonality as sarima

# host in port za flask server
HOST = "192.168.0.19"
PORT = 3000

# pot do datoteke s kljucem do firebaseove oblacne baze in url baze
KEY_FILENAME = "firebase/firebase-admin_private_key.json"
KEY_PATH = os.path.join(DIR, KEY_FILENAME)
from secret import DATABASE_URL

# formatiranje stringov z datumi, locale nastavljen na SI
DATE_FORMAT_VERBOSE = "%A, %d %b %H:%M"
DATE_FORMAT_SHORT = "%d.%m %H:%M"
locale.setlocale(locale.LC_ALL, locale="Slovenian")

# zagon clienta od firebase
cred = credentials.Certificate(KEY_PATH)
firebase_admin.initialize_app(cred, {
    "databaseURL": DATABASE_URL
})
# referenca do baze
DATABASE_PATH_MEASURMENTS = "/measurments"
ref = db.reference(DATABASE_PATH_MEASURMENTS)

# inicializacija streznika
app = Flask(__name__)

# funkcija preveri ce so meritve casovno povezane
def checkForCorrectDates(first, last, samplesLen, minDiff):
    # dovolj vzorcev
    if samplesLen != minDiff:
        return False

    # ni lukenj vmes
    diff = last - first
    hoursDiff = diff.days * 24 + diff.seconds // 3600
    if hoursDiff == samplesLen - 1:
        return True
    else:
        return False

# htttp response domov
@app.route("/")
def responseHome():
    # pridobim "snapshot" zadnje meritve iz baze
    snapshot = ref.order_by_key().limit_to_last(1).get()
    # zanimiv trik za unpack dictionary-ja
    key_ISOdate = list(snapshot.keys())[0]
    subJSON = list(snapshot.values())[0]
    
    # shranim v primerne spremenljivke
    dateObj = datetime.fromisoformat(key_ISOdate)
    dateString = dateObj.strftime(DATE_FORMAT_VERBOSE)
    temperature = subJSON["temperature"]
    humidity = subJSON["humidity"]

    # prva crko dateString postavim v veliko zacetnico
    dateString = dateString[0].upper() + dateString[1 : ]
    
    # template za frontend stran home. Flask/jinja avtomatsko pogleda v "templates" folder
    return render_template("home.html", date=dateString, temperature=temperature, humidity=humidity)

# http response loading screen za napovedi
@app.route("/naloziNapoved")
def responseLoadPrediction():
    return render_template("load.html")

# http response napovedi
@app.route("/napoved")
def responsePrediction():
    # stevilo meritev in ustvarim numpy polje
    numOfModels = 6
    numOfMeasurmentsLSTM = 5 * 24
    numOfMeasurmentsSARIMA = 10 * 24
    numOfVariables = 1
    decimalAccuracy = 2
    mostRecentReadingIndex = -6
    readings = np.empty((numOfMeasurmentsSARIMA, numOfVariables), np.float)

    # pridobim "snapshot" zadnjih-numOfMeasurments meritev
    snapshot = ref.order_by_key().limit_to_last(numOfMeasurmentsSARIMA).get()

    # loop cez pridobljene poizvedbe, shranim v polja
    samplesLen = 0
    for key_ISOdate, subJSON in snapshot.items():
        # shrani prvi in zadnji datum
        if samplesLen == 0:
            firstDate = datetime.fromisoformat(key_ISOdate)
        elif samplesLen + 1 == len(snapshot):
            lastDate = datetime.fromisoformat(key_ISOdate)

        temperature = subJSON["temperature"]
        humidity = subJSON["humidity"]

        readings[samplesLen, 0] = temperature
        if numOfVariables == 2:
            readings[samplesLen, 1] = humidity
        samplesLen += 1

    # preveri ce je dovolj meritev pred napovedjo
    if not checkForCorrectDates(firstDate, lastDate, samplesLen, numOfMeasurmentsSARIMA):
        return render_template("infoForecast.html")

    # napoved lstm (z vsemi 6 modeli)
    predictionLSTMrounded = np.empty(numOfModels, dtype=np.float)

    for i in range(numOfModels):
        lstm = myNeuralNet.NeuralNet(indexOfFutureSampleListToPredict=i, loadModel=True)
        predictionLSTM = lstm.generatePrediction(input=readings[0 : numOfMeasurmentsLSTM], hoursPast=numOfMeasurmentsLSTM)
        predictionLSTMrounded[i] = predictionLSTM[0]
    
    predictionLSTMrounded = np.round(predictionLSTMrounded, decimalAccuracy)

    #napoved sarima
    predictionSARIMA = sarima.generatePredictionFromData(input=readings)
    predictionSARIMArounded = np.round(predictionSARIMA, decimalAccuracy)

    # template za frontend load/napoved. Flask/jinja avtomatsko pogleda v "templates" folder
    return render_template("forecast.html", temperatures=json.dumps(list(readings[mostRecentReadingIndex : , 0])), predictionLSTM=json.dumps(list(predictionLSTMrounded)), \
    predictionSARIMA=json.dumps(list(predictionSARIMArounded)))

# http response graf
@app.route("/pretekleMeritve")
def responseChart():
    # stevilo meritev in ustvarim numpy polje za meritve in datume
    numOfMeasurments = 48
    readings = np.empty((numOfMeasurments, 2), np.float)
    dates = np.empty(numOfMeasurments, np.object)

    # pridobim "snapshot" zadnjih-numOfMeasurments meritev z datumi
    snapshot = ref.order_by_key().limit_to_last(numOfMeasurments).get()
    # loop cez pridobljene poizvedbe, shranim v polja
    samplesLen = 0
    for key_ISOdate, subJSON in snapshot.items():
        dateObj = datetime.fromisoformat(key_ISOdate)
        temperature = subJSON["temperature"]
        humidity = subJSON["humidity"]

        readings[samplesLen, 0] = temperature
        readings[samplesLen, 1] = humidity
        dates[samplesLen] = dateObj.strftime(DATE_FORMAT_SHORT)
        samplesLen += 1

    # template za frontend graf. Flask/jinja avtomatsko pogleda v "templates" folder
    return render_template("pastMeasurments.html", dates=json.dumps(list(dates)), temperatures=json.dumps(list(readings[ : , 0])), humidities=json.dumps(list(readings[ : , 1])))

if __name__ == "__main__":
    app.run(HOST, PORT)