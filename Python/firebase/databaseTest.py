from datetime import datetime
import firebase_admin
from firebase_admin import credentials
from firebase_admin import db
import numpy as np
import os

from secret import DATABASE_URL
KEY_FILENAME = "firebase-admin_private_key.json"
KEY_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), KEY_FILENAME)

cred = credentials.Certificate(KEY_PATH)
firebase_admin.initialize_app(cred, {
    "databaseURL": DATABASE_URL
})

ref = db.reference("/measurments")

# vrne zadnjih 120 vnosov (ce zbrisem limit_to_first(10))
def readFromDatabase(size, withDates=True):
    readings = np.empty((size, 2), np.float)
    if withDates:
        dates = np.empty(size, np.object)

    snapshot = ref.order_by_key().limit_to_first(10).get()
    i = 0
    for key_ISOdate, subJSON in snapshot.items():
        dateObj = datetime.fromisoformat(key_ISOdate)
        temperature = subJSON["temperature"]
        humidity = subJSON["humidity"]

        readings[i, 0] = temperature
        readings[i, 1] = humidity
        if withDates:
            dates[i] = key_ISOdate

        print("datum: {0}, temperatura: {1}, vlaznost: {2}".format(dateObj, temperature, humidity))
        i += 1

def writeToDatabse():
    import datetime
    import pandas as pd
    from myGlobals import DATA_SAMPLES

    df = pd.read_csv(DATA_SAMPLES)
    x = df.values[:240:2]

    for row in x:
        date_time_obj = datetime.datetime.strptime(row[0], '%Y-%m-%d %H:%M')
        #key_seconds = int((date_time_obj - datetime.datetime(1970,1,1)).total_seconds())
        key_ISO8601_date = date_time_obj.isoformat()

        dataJson = {key_ISO8601_date: {"temperature": row[1], "humidity": row[2]}}
        ref.update(dataJson)