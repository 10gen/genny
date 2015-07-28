#include "forN.hpp"
#include<stdlib.h>

namespace mwg {

    forN::forN(YAML::Node &node) {
        // need to set the name
        // these should be made into exceptions
        // should be a map, with type = forN
        if (!node) {
            cerr << "ForN constructor and !node" << endl;
            exit(EXIT_FAILURE);
            }
        if (!node.IsMap()) {
            cerr << "Not map in forN type initializer" << endl;
            exit(EXIT_FAILURE);
        }
        if (node["type"].Scalar() != "forN") {
                cerr << "ForN constructor but yaml entry doesn't have type == forN" << endl;
                exit(EXIT_FAILURE);
            }
        name = node["name"].Scalar();
        nextName = node["next"].Scalar();
        //cout << "In forN constructor. Name: " << name << ", nextName: " << nextName << endl;
        // Need to read in the workload, or implicitly make one. 
        if (!node["workload"]) {
                cerr << "ForN constructor but yaml entry doesn't have a workload entry" << endl;
                exit(EXIT_FAILURE);
        }
        N = node["N"].as<uint64_t>();
        auto yamlWorkload = node["workload"];
        myWorkload = workload(yamlWorkload);
    }

    // Execute the node
    void forN::execute(shared_ptr<threadState> myState) {
        // execute the workload N times
        for (int i = 0; i < N; i++) {
            cout << "In forN and executing interation " << i << endl;
            myWorkload.execute(myState->conn);
        }
    }
}
