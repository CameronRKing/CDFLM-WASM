#include "NDPSO.h"

#include <map>
#include <ctime>
#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include "Utils.h"
#include "Particle.h"
#include "ObjectiveStrategy.h"
#include "AssignmentStrategy.h"
using namespace std;

/* functional object for getGlobalBest() && getGlobalWorst() */
struct minFitness {
    bool operator() (Particle left, Particle right) { return left.fitness < right.fitness; }
};

/**
 * Default constructor
 * Sets parameters to defaults found in NDPSO.h
 * does NOT initialize swarm!
 **/
NDPSO::NDPSO() {
    this->swarmSize = SWARM_SIZE;
    this->social    = SOCIAL;
    this->cognitive = COGNITIVE;
    this->inertia   = INERTIA;
    this->initialInertia   = INERTIA;
    this->inertialDiscount = INERTIAL_DISCOUNT;
    this->maxIterations    = MAX_ITERATIONS;
}

/**
 * Constructor for just setting the number of iterations for which we run the PSO
 * does NOT initialize swarm!
 *
 * @param int maxIterations
 **/
NDPSO::NDPSO(int maxIterations) {
    this->swarmSize = SWARM_SIZE;
    this->social    = SOCIAL;
    this->cognitive = COGNITIVE;
    this->inertia   = INERTIA;
    this->initialInertia   = INERTIA;
    this->inertialDiscount = INERTIAL_DISCOUNT;
    this->maxIterations    = maxIterations;
}

/**
 * Overloaded constructor that accepts values for parameters
 * does NOT initialize swarm!
 * 
 * @param float social
 * @param float cognitive
 * @param float inertia
 * @param float inertialDiscount
 * @param int   swarmSize
 **/
NDPSO::NDPSO(float social, float cognitive, float inertia, float inertialDiscount, int swarmSize, int maxIterations) {
    this->swarmSize = swarmSize;
    this->social    = social;
    this->cognitive = cognitive;
    this->inertia   = inertia;
    this->initialInertia   = inertia;
    this->inertialDiscount = inertialDiscount;
    this->maxIterations    = maxIterations;
}

/**
 * Returns a JSON object that represents the parameters of this algorithm for logging
 *
 * @return { inertia, cognitive, social, inertialDiscount, swarmSize, maxIterations }
 **/
string NDPSO::getJSONParameters() {
    string json = "{";
    json += "inertia: "            + to_string(this->initialInertia);
    json += ", cognitive: "        + to_string(this->cognitive);
    json += ", social: "           + to_string(this->social);
    json += ", inertialDiscount: " + to_string(this->inertialDiscount);
    json += ", swarmSize: "        + to_string(this->swarmSize);
    json += ", maxIterations: "    + to_string(this->maxIterations);
    json += "}";
    return json;
}

/**
 * Runs through 125 parameter sets, 10 trials each.
 * Data is meant to be saved and analyzed with Python scripts
 * This object will NOT have the optimal parameter set
 * when this function is finished!
 *
 * @param const ProblemData data
 * @return void
 **/
void NDPSO::searchParameters(const ProblemData data) {
    ProblemResults results;
    int done = 0;
    for (float c1 = 0.1; c1 <= 0.9; c1 += 0.2) {
        this->setInertia(c1);
        for (float c2 = 0.1; c2 <= 0.9; c2 += 0.2) {
            this->setCognitive(c2);
            for (float c3 = 0.1; c3 <= 0.9; c3 += 0.2) {
                this->setSocial(c3);
                for (int i = 0; i < 10; i++) {
                    this->listener->handleAlgorithm(this, data.name, data.type, data.objType);
                    results = this->optimize(data);
                    this->listener->handleResults(results);
                    ++done;
                }
                cout << done << " done" << endl;
            }
        }
    }
}

/**
 * Optimizes a given problem
 * initializes swarm
 *  todo: log intermediate steps to a logfile (database table?)
 * @param ProblemData problem
 * @return ProblemResults
 **/
ProblemResults NDPSO::optimize(ProblemData data) {
    // initial setup
    this->data = data;
    this->comparator.setType(data.objType);
    this->initSwarm();
    Particle gBest = getGlobalBest();   // global best; across current iteration
    Particle uBest = gBest;             // universal best; across all iterations

    // re-initialize our potentially already discounted inertia to its starting value
    this->inertia = this->initialInertia;

    // ladies and gentlemen, start your engines!
    clock_t begin = clock();
    for (int count = 1; count <= this->maxIterations; count++) {
        inertia *= inertialDiscount;
        for (Particle &particle : this->swarm) {
            particle.update(gBest);
        }
        gBest = getGlobalBest();
        if (this->comparator(gBest.fitness, uBest.fitness)) {
            uBest = gBest;
        }

        listener->handleParticle(&uBest, count);
    }

    ProblemResults results {
                               float(clock() - begin) / CLOCKS_PER_SEC,
                               uBest.fitness,
                               uBest.position,
                               uBest.getCustomerAssignments(),  // customer assignments are calculated deterministically
                               data.type,                       // we don't save them in order to optimize space, but we can recalculate them
                               data.objType
                           };                                   
    return results;
}

/**
 * Returns the current global best in the swarm
 * @return Particle globalBest
 **/
Particle NDPSO::getGlobalBest() {
    minFitness func;
    switch (this->data.objType) {
        case MINIMIZE:
            return *min_element(this->swarm.begin(), this->swarm.end(), func);
        case MAXIMIZE:
            return *max_element(this->swarm.begin(), this->swarm.end(), func);
        default:
            throw "NDPSO::getGlobalBest(): unrecognized this->data.objType!";
 
    }
}

/**
 * Overload of Algorithm::calcObjective to accept only a facilities vector because Particles aren't tracking customer assignments
 *
 * @param vector<int>& facilities
 * @return int objective value for the problem type
 **/
int NDPSO::calcObjective(const vector<int>& facilities) {
    vector<int> customerAssignments = AssignmentStrategy::assign(this->data.costs, facilities, this->data.type);
    return ObjectiveStrategy::calcObjective(this->data.costs, customerAssignments, this->data.type);
}

/**
 * Initializes a swarm with random positions
 *      --> this variation doesn't have velocities
 **/
void NDPSO::initSwarm() {
    if (this->swarm.size() > 0) { // clear old swarms from previous problems
        this->swarm.clear();
    }

    int numDimensions = this->data.numFacilities;
    int possibleFacs  = this->data.numCustomers;
    for (int i = 0; i < this->swarmSize; i++) {
        Particle particle (numDimensions, possibleFacs, this);
        this->swarm.push_back(particle);
    }
}
