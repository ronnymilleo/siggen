import numpy as np
import matplotlib.pyplot as plt


def convolve(signal, filter):
    return np.convolve(signal, filter, mode='full')


def RRC_filter():
    beta = 0.2
    span = 10
    SPS = 8

    # build the time indexing
    nList = np.arange(-(span * SPS) / 2, (span * SPS) / 2 + 1)

    # pre-allocate memory for weights
    weights = np.zeros(len(nList))

    # compute the weights on a sample by sample basis
    for index in range(len(nList)):

        # select the time index
        n = nList[index]

        # design equations
        if (n == 0):
            weights[index] = (1 / np.sqrt(SPS)) * ((1 - beta) + (4 * beta / np.pi))
        elif (np.abs(n * 4 * beta) == SPS):
            weights[index] = (beta / np.sqrt(2 * SPS)) * (
                    (1 + (2 / np.pi)) * np.sin(np.pi / (4 * beta)) + (1 - (2 / np.pi)) * np.cos(np.pi / (4 * beta)))
        else:
            weights[index] = (1 / np.sqrt(SPS)) * ((np.sin(np.pi * n * (1 - beta) / SPS)) + (4 * beta * n / SPS) * (
                np.cos(np.pi * n * (1 + beta) / SPS))) / ((np.pi * n / SPS) * (1 - (4 * beta * n / SPS) ** 2))

    # scale the weights to 0 dB gain at f=0
    weights = weights / np.sqrt(SPS)

    plt.plot(weights)
    plt.show()

    return weights


if __name__ == "__main__":
    filter = RRC_filter()

    dataIn = [0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1]

    data_nrz = []
    for i in range(0, len(dataIn)):
        if dataIn[i] == 0:
            for _ in range(0, 8):
                data_nrz.append(1)
        else:
            for _ in range(0, 8):
                data_nrz.append(-1)

    result = convolve(data_nrz, filter)

    plt.figure(2)
    plt.plot(result)
    plt.show()
