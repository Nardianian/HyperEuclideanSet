#include "EuclideanSet.h"

// Implementation of the Euclidean class
Euclidean::Euclidean(int pulses, int steps) : pulses(pulses), steps(steps) {}

void Euclidean::computeClassicEuclidean() {
    // Implementation of the classical Euclidean algorithm
    if (pulses > steps)
        pulses = steps;

    std::string x = "1";
    int x_amount = pulses;
    std::string y = "0";
    int y_amount = steps - pulses;

    do {
        int x_temp = x_amount;
        int y_temp = y_amount;
        std::string y_copy = y;

        if (x_temp >= y_temp) {
            x_amount = y_temp;
            y_amount = x_temp - y_temp;
            y = x;
        }
        else {
            x_amount = x_temp;
            y_amount = y_temp - x_temp;
        }

        x = x + y_copy;
    } while (x_amount > 1 && y_amount > 1);

    sequence.clear();
    for (int i = 1; i <= x_amount; ++i) sequence.push_back(1);
    for (int i = 1; i <= y_amount; ++i) sequence.push_back(0);
}

std::vector<int> Euclidean::generateSequence()
{
    sequence.clear();

    // Clamp di sicurezza
    if (steps <= 0)
        return sequence;

    if (pulses < 0)
        pulses = 0;
    if (pulses > steps)
        pulses = steps;

    // Caso limite
    if (pulses == 0)
    {
        sequence.resize(steps, 0);
        return sequence;
    }

    if (pulses == steps)
    {
        sequence.resize(steps, 1);
        return sequence;
    }

    // Bjorklund's algorithm (simplified iterative form)
    std::vector<int> counts;
    std::vector<int> remainders;

    int divisor = steps - pulses;
    remainders.push_back(pulses);
    int level = 0;

    while (true)
    {
        counts.push_back(divisor / remainders[level]);
        remainders.push_back(divisor % remainders[level]);
        divisor = remainders[level];
        level++;

        if (remainders[level] <= 1)
            break;
    }

    counts.push_back(divisor);

    // Recursive build
    std::function<void(int)> build = [&](int lvl)
    {
        if (lvl == -1)
        {
            sequence.push_back(0);
        }
        else if (lvl == -2)
        {
            sequence.push_back(1);
        }
        else
        {
            for (int i = 0; i < counts[lvl]; ++i)
                build(lvl - 1);

            if (remainders[lvl] != 0)
                build(lvl - 2);
        }
    };

    build(level);

    // Final normalization (safety)
    if ((int)sequence.size() > steps)
        sequence.resize(steps);

    while ((int)sequence.size() < steps)
        sequence.push_back(0);

    return sequence;
}

// Implementation of the HyperEuclidean class
HyperEuclidean::HyperEuclidean(int pulses, int steps, int depth)
    : Euclidean(pulses, steps), depth(depth) {
}

std::vector<int> HyperEuclidean::generateSequence() {
    computeHyperEuclidean();
    return sequence;
}

void HyperEuclidean::computeHyperEuclidean()
{
    sequence.clear();

    // --- 1. Euclidean base (binary) ---
    Euclidean base(pulses, steps);
    std::vector<int> basePattern = base.generateSequence();

    // Extract onset positions
    std::vector<int> onsets;
    for (int i = 0; i < (int)basePattern.size(); ++i)
        if (basePattern[i] == 1)
            onsets.push_back(i);

    if (onsets.empty())
    {
        sequence.resize(steps, 0);
        return;
    }

    // --- 2. Hyper depth: reduction via IOI ---
    for (int d = 1; d < depth; ++d)
    {
        int n = (int)onsets.size();
        if (n <= 1)
            break;

        // Calculate IOI
        std::vector<int> ioi;
        for (int i = 0; i < n - 1; ++i)
            ioi.push_back(onsets[i + 1] - onsets[i]);

        // wraparound
        ioi.push_back(steps - onsets.back() + onsets.front());

        // Number of selections (progressive reduction)
        int k = std::max(1, n / 2);

        // Euclidean on IOI
        Euclidean ioiEuclid(k, n);
        std::vector<int> selector = ioiEuclid.generateSequence();

        // Hierarchical selection (as operator-)
        std::vector<int> nextOnsets;
        for (int i = 0; i < n; ++i)
        {
            if (selector[i] == 1)
                nextOnsets.push_back(onsets[i]);
        }

        onsets = nextOnsets;
        if (onsets.empty())
            break;
    }

    // --- 3. Final binary pattern reconstruction ---
sequence.resize(steps, 0);
velocities.resize(steps, 0);

if (onsets.size() == 1)
{
    int idx = onsets[0];
    sequence[idx] = 1;
    velocities[idx] = 110;
    return;
}

// Calculate final IOIs
std::vector<int> ioi;
for (size_t i = 0; i < onsets.size() - 1; ++i)
    ioi.push_back(onsets[i + 1] - onsets[i]);

ioi.push_back(steps - onsets.back() + onsets.front());

//IOI normalization -> velocity
int maxIOI = *std::max_element(ioi.begin(), ioi.end());
int minVel = 40;
int maxVel = 115;

for (size_t i = 0; i < onsets.size(); ++i)
{
    int idx = onsets[i];
    sequence[idx] = 1;

    float norm = (float)ioi[i] / (float)maxIOI;
    velocities[idx] = juce::jlimit(
        minVel,
        maxVel,
        (int)(minVel + norm * (maxVel - minVel))
    );
}
}

void HyperEuclidean::rotateSet(std::vector<int>& set, int n, int r) {
    int rr = r % n;
    if (rr < 0) rr += n;
    if (rr == 0) return;

    int cut = n - rr;
    size_t idx = 0;
    while (idx < set.size() && set[idx] < cut) ++idx;

    std::vector<int> rotated;
    rotated.reserve(set.size());

    // First part rotated
    for (size_t i = idx; i < set.size(); ++i)
        rotated.push_back(set[i] + rr - n);

    // Then unrotated part
    for (size_t i = 0; i < idx; ++i)
        rotated.push_back(set[i] + rr);

    set = rotated;
}

std::vector<int> HyperEuclidean::selectByIndex(const std::vector<int>& source, const std::vector<int>& indices) {
    std::vector<int> out;
    for (int idx : indices) {
        out.push_back(source[idx]);
    }
    return out;
}

std::vector<int> HyperEuclidean::computeIOI(const std::vector<int>& set) {
    std::vector<int> iois;
    for (size_t i = 1; i < set.size(); ++i) {
        iois.push_back(set[i] - set[i - 1]);
    }
    return iois;
}
