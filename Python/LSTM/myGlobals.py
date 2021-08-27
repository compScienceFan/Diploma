import os

CURR_DIR = os.path.dirname(os.path.realpath(__file__))
ARCHIVE_FOLDER = os.path.join(CURR_DIR, "Archive")
RESOURCES_FOLDER = os.path.join(CURR_DIR, "Resources")
DATA_FOLDER = os.path.join(RESOURCES_FOLDER, "Data")
HISTORY_FOLDER = os.path.join(RESOURCES_FOLDER, "History")
MODEL_FOLDER = os.path.join(RESOURCES_FOLDER, "Model")
SCALER_FOLDER = os.path.join(RESOURCES_FOLDER, "Scaler")

DATA_SAMPLES = os.path.join(DATA_FOLDER, "dataSamples.csv")
HISTORY = os.path.join(HISTORY_FOLDER, "history")
MODEL = os.path.join(MODEL_FOLDER, "model_LSTM")
SCALER_TEMPERATURE = os.path.join(SCALER_FOLDER, "minMaxScaler_temperature")
SCALER_TIME = os.path.join(SCALER_FOLDER, "minMaxScaler_time")

HISTORY_EXTENSION = ".csv"
MODEL_EXTENSION = ".h5"
SCALER_EXTENSION = ".pkl"

def joinFileExtension(file, index):
    if index == -1:
        strIndex = ""
    else:
        strIndex = str(index)

    if file == HISTORY:
        return (file + strIndex + HISTORY_EXTENSION)
    elif file == MODEL:
        return (file + strIndex + MODEL_EXTENSION)
    else:
        return (file + strIndex + SCALER_EXTENSION)

NUM_SAMPLES_PAST = 5 * 24 # [ura]
SAMPLES_IN_FUTURE_LIST = [1, 24, 48, 72, 96, 120] # [ura]
NUM_OF_MODELS = len(SAMPLES_IN_FUTURE_LIST)
TRAIN_RATIO = 0.8
NUM_OF_INPUT_VARIABLES = 1