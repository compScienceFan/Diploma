import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from statsmodels.tsa.stattools import adfuller, acf, pacf
from statsmodels.tsa.statespace.sarimax import SARIMAX

def plotGraph(title, y, label, xName, yName, y2=None, label2=None):
    plt.figure(figsize=(10,4))
    plt.plot(y, "b", label=label)
    if y2 is not None:
        plt.plot(y2, "r", label=label2)
    plt.title(title)
    plt.xlabel(xName)
    plt.ylabel(yName)
    plt.legend(loc="upper left", fontsize=14)
    plt.show()

def plotBar(title, x, y, label, xName, yName):
    plt.figure(figsize=(10,4))
    plt.bar(x, y, color="b", label=label)
    plt.title(title)
    plt.xlabel(xName)
    plt.ylabel(yName)
    plt.legend(loc="upper right", fontsize=14)
    plt.show()

def generatePredictionFromData(input, steps=5*24):
    # redi
    order = (1, 0, 2)
    seasonal_order = (3, 1, 0, 24)

    # natreniraj model
    model = SARIMAX(input, order=order, seasonal_order=seasonal_order)
    model_fitted = model.fit(disp=0)

    # napovedi
    predictions = model_fitted.forecast(steps=steps)

    return predictions

if __name__ == "__main__":
    import os
    CURR_DIR = os.path.dirname(os.path.realpath(__file__))
    DATA_SAMPLES = os.path.join(CURR_DIR, "dataSamples.csv")

    # preberi csv (urni format)
    df = pd.read_csv(DATA_SAMPLES)
    t = df["T [°C]"].values[::2]

    # omejitev na 10 dni (train) + 5 dni napoved
    TRAIN_DAYS = 10
    TEST_DAYS = 5
    NUM_OF_DAYS = TRAIN_DAYS + TEST_DAYS
    OFFSET_START = 80000 # v urah
    t_limited = t[OFFSET_START : OFFSET_START + (NUM_OF_DAYS * 24)]
    plotGraph("Časovna vrsta", t_limited, "temperatura", "vzorec (v urah)", "temperatura")

    # ADF (augmented dickey-fuller) test, ki preveri ce so podatki stacionarni
    print("p-value(proti referenci 0.05) originala: ", adfuller(t_limited)[1]) # ker je p vecji od 0.05 NI stacionaren
    # vzamemo prvo razliko, da dobimo stacionarne podatke, dodatno se preverimo z ADF
    t_first_diff = np.diff(t_limited)
    plotGraph("Stacionarna časovna vrsta (s prvo razliko)", t_first_diff, "temperatura", "vzorec (v urah)", "temperatura")
    print("p-value(proti referenci 0.05) prve razlike: ", adfuller(t_first_diff)[1], "\n") # ker je p manjsi od 0.05 lahko potrdimo, da JE stacionaren

    # acf
    acf_vals = acf(t_first_diff)
    print(len(acf_vals))
    plotBar("Avtokorelacija - ACF", range(len(acf_vals)), acf_vals, "vrednosti ACF", "vzorec", "vrednost")

    # pacf
    pacf_vals = pacf(t_first_diff)
    plotBar("Delna avtokorelacija - PACF", range(len(pacf_vals)), pacf_vals, "vrednosti PACF", "vzorec", "vrednost")

    # train in test(primerjava)
    TRAIN_HOURS = TRAIN_DAYS * 24
    TEST_HOURS = TEST_DAYS * 24
    train = t_limited[0 : TRAIN_HOURS]
    test = t_limited[TRAIN_HOURS : ]

    # redi
    #order = (0, 1, 1)
    #seasonal_order = (1, 0, 0, 24)
    #order = (1, 0, 0)
    #seasonal_order = (1, 1, 1, 24)
    order = (1, 0, 2)
    seasonal_order = (3, 1, 0, 24)

    # natreniraj model
    model = SARIMAX(train, order=order, seasonal_order=seasonal_order)
    model_fitted = model.fit()
    #print(model_fitted.summary())

    # napovedi
    predictions = model_fitted.forecast(steps=TEST_HOURS)
    predictions = pd.Series(predictions, index=range(TRAIN_HOURS, TRAIN_HOURS + TEST_HOURS))
    errors = t_limited[TRAIN_HOURS : ] - predictions
    plotGraph("Napake", errors, "odstopanja", "vzorec", "temp")
    plotGraph("Napoved", t_limited, "dejanske vrednosti", "vzorec", "temp", predictions, "napovedi")

    # MAE
    mae = np.mean(np.abs(errors))
    print("MAE:", mae)

    # MSE
    mse = np.mean(errors**2)
    print("MSE:", mse)

    """ 
    # MODEL Z AVTOMATSKO DOLOCITVIJO PARAMETROV (KRITERIJ AIC)
    import pmdarima as pm

    model = pm.auto_arima(train, start_p=0, start_q=0, max_P=3, max_Q=3,
                           max_p=3, max_q=3, m=24,
                           start_P=0, start_Q=0, seasonal=True,
                           d=0, D=1, trace=True, 
                           stepwise=True)
    predictions = model.predict(n_periods=5*24)
    errors = t_limited[TRAIN_HOURS : ] - predictions
    plotGraph("Napake", errors, "odstopanja", "vzorec", "temp")
    plotGraph("Napoved", t_limited, "dejanske vrednosti", "vzorec", "temp", predictions, "napovedi")

    # MAE
    mae = np.mean(np.abs(errors))
    print("MAE:", mae)

    # MSE
    mse = np.mean(errors**2)
    print("MSE:", mse)"""