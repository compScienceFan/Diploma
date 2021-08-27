print("\n")
from myGlobals import NUM_OF_MODELS
from myNeuralNet import NeuralNet

if __name__ == "__main__":
    neuralNetworks = list()

    for i in range(NUM_OF_MODELS):
        neuralNetworks.append(NeuralNet(i))
        neuralNetworks[i].train()

    for net in neuralNetworks:
        net.plotTrainingAndValidationMetrics()