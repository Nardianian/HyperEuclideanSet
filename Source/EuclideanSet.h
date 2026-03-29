#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <algorithm>

// Base class for the computation of classical Euclidean sequences
class Euclidean {
public:
    Euclidean(int pulses = 1, int steps = 1);
    virtual ~Euclidean() {}

    // Function to calculate a Euclidean sequence
    virtual std::vector<int> generateSequence();

protected:
    int pulses;  // Number of pulses
    int steps;   // Number of total steps
    std::vector<int> sequence; // Resulting sequence

    void computeClassicEuclidean(); // Classical Euclidean algorithm
};

// Derived class for Euclidean Hypereuclidean (HyperEuclidean)
class HyperEuclidean : public Euclidean {
public:
    HyperEuclidean(int pulses = 1, int steps = 1, int depth = 1);
    virtual ~HyperEuclidean() {}

    std::vector<int> generateSequence() override;
    std::vector<int> velocities;

private:
    int depth;  // Depth of hypereuclidean calculus
    void computeHyperEuclidean();
    void rotateSet(std::vector<int>& set, int n, int r);
    std::vector<int> selectByIndex(const std::vector<int>& source, const std::vector<int>& indices);
    std::vector<int> computeIOI(const std::vector<int>& set);
};


