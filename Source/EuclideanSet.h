#pragma once

#include <JuceHeader.h>
#include <vector>
#include <string>
#include <algorithm>

class Euclidean {
public:
    Euclidean(int pulses = 1, int steps = 1);
    virtual ~Euclidean() {}

    virtual std::vector<int> generateSequence();

protected:
    int pulses;  // Pulses Number
    int steps;   // Steps Number
    std::vector<int> sequence; // Sequence

    void computeClassicEuclidean(); // Euclidean classic
};

// HyperEuclidean
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

