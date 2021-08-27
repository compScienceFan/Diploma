import os
import pandas as pd

from myGlobals import ARCHIVE_FOLDER, DATA_SAMPLES

def GenerateCsv():
    # preberi vse datoteke, vrstni red po datumu
    files = sorted(os.listdir(ARCHIVE_FOLDER), key=lambda x: os.path.getctime(os.path.join(ARCHIVE_FOLDER, x)), reverse=True)

    # zdruzi vse meritve v eno
    df = pd.read_csv(os.path.join(ARCHIVE_FOLDER, files[0]))
    files.pop(0)
    for f in files:
        df2 = pd.read_csv(os.path.join(ARCHIVE_FOLDER, f))
        df = df.append(df2)

    # zbrisi vrstice brez podatkov o temperaturi in prva dva stolpca (id, postaja)
    df = df.dropna()
    df = df.drop(df.columns[[0, 1]], axis=1)
    # preimenovanje stolpcev
    df = df.rename(columns={" valid": "date", "rel. vla. [%]": "H [%]"})
    # izpis prvih vrstic in statistike
    print(df.head())
    print("\n--------------------------------------\n", df.describe())

    # sprememba formata datuma 2020-01-01 v 20200101
    #df["date"] = df["date"].str.replace("-", "").astype(int)

    # shrani v nov csv file (brez index stolpca)
    df.to_csv(DATA_SAMPLES, index=False)
    print("----------------------------------------------------------------")
    print("Baza vzorcev uspesno sprocesirana in shranjena v", DATA_SAMPLES)
    print("----------------------------------------------------------------")