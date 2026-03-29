#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <algorithm>

// Classe base per il calcolo delle sequenze Euclidee classiche
class Euclidean {
public:
    Euclidean(int pulses = 1, int steps = 1);
    virtual ~Euclidean() {}

    // Funzione per calcolare una sequenza Euclidea
    virtual std::vector<int> generateSequence();

protected:
    int pulses;  // Numero di pulsazioni
    int steps;   // Numero di step totali
    std::vector<int> sequence; // Sequenza risultante

    void computeClassicEuclidean(); // Algoritmo Euclideo classico
};

// Classe derivata per Euclidean ipereuclideo (HyperEuclidean)
class HyperEuclidean : public Euclidean {
public:
    HyperEuclidean(int pulses = 1, int steps = 1, int depth = 1);
    virtual ~HyperEuclidean() {}

    std::vector<int> generateSequence() override;
    std::vector<int> velocities;

private:
    int depth;  // Profondità del calcolo ipereuclideo
    void computeHyperEuclidean();
    void rotateSet(std::vector<int>& set, int n, int r);
    std::vector<int> selectByIndex(const std::vector<int>& source, const std::vector<int>& indices);
    std::vector<int> computeIOI(const std::vector<int>& set);
};


