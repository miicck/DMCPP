/*
    DMCPP
    Copyright (C) 2019 Michael Hutcheon (email mjh261@cam.ac.uk)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    For a copy of the GNU General Public License see <https://www.gnu.org/licenses/>.
*/

#include <mpi.h>
#include <vector>
#include <sstream>
#include <iterator>

#include "params.h"
#include "particle.h"
#include "walker.h"

// Default values for program parameters
bool   params::write_wavefunction   = true;
bool   params::exchange_moves       = true;
bool   params::correct_seperations  = false;
int    params::pid                  = 0;
int    params::np                   = 1;
int    params::dimensions           = 3;
int    params::target_population    = 1000;
int    params::dmc_iterations       = 1000;
double params::max_pop_ratio        = 4.0;
double params::min_pop_ratio        = 0.5;
double params::exchange_prob        = 0.5;
double params::tau                  = 0.01;
double params::tau_c_ratio          = 1.0;
double params::pre_diffusion        = 1.0;
double params::trial_energy         = 0.0;
std::string params::cancel_scheme   = "voronoi";
std::vector<external_potential*> params::potentials;
std::vector<particle*>           params::template_system;
std::vector<int> params::exchange_pairs;
std::vector<int> params::exchange_values;
output_file params::wavefunction_file;
output_file params::evolution_file;
output_file params::progress_file;
output_file params::error_file;

// The value of clock() at startup
int start_clock;

// Split a string on whitespace
std::vector<std::string> split_whitespace(std::string to_split)
{
        // Iterator magic
        std::istringstream buffer(to_split);
        std::vector<std::string> split;
        std::copy(std::istream_iterator<std::string>(buffer),
                  std::istream_iterator<std::string>(),
                  std::back_inserter(split));
        return split;
}

void params :: parse_particle(std::vector<std::string> split)
{
    // Parse a particle from the format
    // particle name mass charge half_spins x1 x2 x3 ...
    particle* p   = new particle();
    p->name       = split[1];
    p->mass       = std::stod(split[2]);
    p->charge     = std::stod(split[3]);
    p->half_spins = std::stoi(split[4]);
    for (int i=0; i<dimensions; ++i)
        p->coords[i] = std::stod(split[5+i]);
    template_system.push_back(p);
}

void params :: parse_atomic_potential(std::vector<std::string> split)
{
    // Parse an atomic potential from the format
    // atomic_potential charge x1 x2 x3 ...
    
    double charge  = std::stod(split[1]);
    double* coords = new double[dimensions];
    for (int i=0; i<dimensions; ++i)
        coords[i] = std::stod(split[2+i]);
    potentials.push_back(new atomic_potential(charge, coords));
}

// Parse the input file.
void params :: read_input()
{
    std::ifstream input("input");
    for (std::string line; getline(input, line); )
    {
        auto split = split_whitespace(line);
        if (split.size() == 0) continue;
        std::string tag = split[0];

        // Ignore comments
        if (tag.rfind("!" , 0) == 0) continue;
        if (tag.rfind("#" , 0) == 0) continue;
        if (tag.rfind("//", 0) == 0) continue;

        // Read in the dimensionality
        if (tag == "dimensions")
            dimensions = std::stoi(split[1]);

        // Read in the number of DMC walkers and convert
        // to walkers-per-process
        else if (tag == "walkers")
            target_population = std::stoi(split[1])/np;

        // Read in maximum population ratio
        else if (tag == "max_pop_ratio")
            max_pop_ratio = std::stod(split[1]);

        // Read in minimum population ratio
        else if (tag == "min_pop_ratio")
            min_pop_ratio = std::stod(split[1]);

        // Read in the number of DMC iterations
        else if (tag == "iterations")
            dmc_iterations = std::stoi(split[1]);

        // Read in the DMC timestep
        else if (tag == "tau")
            tau = std::stod(split[1]);

        // Read in ratio of tau_c to tau
        else if (tag == "tau_c_ratio")
            tau_c_ratio = std::stod(split[1]);

        // Read in the pre diffusion amount
        else if (tag == "pre_diffusion")
            pre_diffusion = std::stod(split[1]);

        // Read in a particle
        else if (tag == "particle")
            parse_particle(split);
    
        // Add a potential from a grid to the system
        else if (tag == "grid_potential")
            potentials.push_back(new grid_potential(split[1]));

        // Add a harmonic well to the system
        else if (tag == "harmonic_well")
            potentials.push_back(new harmonic_well(std::stod(split[1])));

        // Add an atomic potential to the system
        else if (tag == "atomic_potential")
            parse_atomic_potential(split);

        // Should we write the wavefunctions this run
        else if (tag == "no_wavefunction")
            write_wavefunction = false;

        // Turn off exchange moves
        else if (tag == "no_exchange")
            exchange_moves = false;

        // Read in the exchange probability
        else if (tag == "exchange_prob")
            params::exchange_prob = std::stod(split[1]);

        // Set the cancellation scheme
        else if (tag == "cancel_scheme")
            cancel_scheme = split[1];

        // Turn on seperation corrections
        else if (tag == "seperation_correction")
            correct_seperations = true;
    }

    input.close();

    // Work out exchange properties of the system
    for (unsigned i=0; i<template_system.size(); ++i)
    {
        particle* p1 = template_system[i];
        for (unsigned j=0; j<i; ++j)
        {
            particle* p2 = template_system[j];
            int exchange_sym = p1->exchange_symmetry(p2);

            // These particles cannot be exchanged 
            if (exchange_sym == 0) continue;

            // Record this exchangable pair
            exchange_pairs.push_back(j);
            exchange_pairs.push_back(i);
            exchange_values.push_back(exchange_sym);
        }
    }
}

std::string b2s(bool b)
{
    // Convert a bool to a string
    if (b) return "true";
    else return "false";
}

void params :: output_sim_details()
{
    // Output system information
    progress_file << "System loaded\n";
    progress_file << "    Dimensions            : " << dimensions                 << "\n";
    progress_file << "    Particles             : " << template_system.size()     << "\n";
    progress_file << "    Total charge          : " << total_charge()             << "\n";
    progress_file << "    Exchange pairs        : " << exchange_values.size()     << "\n"; 
    progress_file << "    Exchange moves        : " << b2s(exchange_moves)        << "\n";
    progress_file << "    Exchange prob         : " << exchange_prob              << "\n";
    progress_file << "    Cancel scheme         : " << cancel_scheme              << "\n";
    progress_file << "    Pre diffusion         : " << pre_diffusion              << "\n";
    progress_file << "    DMC timestep          : " << tau                        << "\n";
    progress_file << "    Cancellation timestep : " << tau*tau_c_ratio
              << " = tau x " << tau_c_ratio << "\n";
        progress_file << "    Seperation correction : " << b2s(correct_seperations)   << "\n";

    progress_file << "    DMC walkers           : " 
              << target_population*np << " (total) "
              << target_population    << " (per process)\n";

    progress_file << "    DMC iterations        : " 
              << dmc_iterations << " => Imaginary time in [0, " 
              << dmc_iterations*tau << "]\n";

    progress_file << "    MPI processes         : " << np  << "\n";
    progress_file << "    Write wavefunction    : " << b2s(write_wavefunction) << "\n";

    // Output a summary of potentials to the progress file
    progress_file << "Potentials\n";
    for (unsigned i=0; i<potentials.size(); ++i)
        progress_file << "    " << potentials[i]->one_line_description() << "\n";

    // Output a summary of particles to the progress file
    progress_file << "Particles\n";
    for (unsigned i=0; i<template_system.size(); ++i)
           progress_file << "    " << i << ": "
                 << template_system[i]->one_line_description() << "\n";

    // Output a summary of exchange information
    progress_file << "Exchange pairs (sign, particle 1, particle 2)\n";
    for (unsigned i=0; i<exchange_values.size(); ++i)
           progress_file << "    " << exchange_values[i] << " "
                 << exchange_pairs[2*i]   << " "
                 << exchange_pairs[2*i+1] << "\n";

    progress_file << "\n";
}

void params::load(int argc, char** argv)
{
    // Get the start time so we can time stuff
    start_clock = clock();

    
    // Initialize mpi
        if (MPI_Init(&argc, &argv) != 0) exit(MPI_ERROR);

        // Get the number of processes and my id within them
        if (MPI_Comm_size(MPI_COMM_WORLD, &np)  != 0) exit(MPI_ERROR);
        if (MPI_Comm_rank(MPI_COMM_WORLD, &pid) != 0) exit(MPI_ERROR);

        // Seed random number generator
        srand(pid*clock());

        // Read our input and setup parameters accordingly 
        // do for each process sequentially to avoid access issues
        for (int pid_read = 0; pid_read < np; ++ pid_read)
        {
                if (pid == pid_read) read_input();
                MPI_Barrier(MPI_COMM_WORLD);
        }

    // Open various output files
    if (pid == 0)
    {
        // Files on the root process
        progress_file.open("progress");
        evolution_file.open("evolution");
    }

    // Files on all processes have their pid appended
    error_file.open("error_"+std::to_string(pid));
    error_file.auto_flush = true;
    wavefunction_file.open("wavefunction_"+std::to_string(pid));
    
    // Output parameters to the progress file
    output_sim_details();
}

double params :: total_charge()
{
    // Work out the total charge on the system
    double sum = 0;
    for (unsigned i=0; i<template_system.size(); ++i)
        sum += template_system[i]->charge;
    return sum;
}

void params::free_memory()
{
    // Close various output files
    progress_file.close();
    evolution_file.close();
    wavefunction_file.close();

    // Free memory in template_system
    for (unsigned i=0; i<template_system.size(); ++i)
        delete template_system[i];

    // Free memory in potentials
    for (unsigned i=0; i<potentials.size(); ++i)
        delete potentials[i];

    // Output info on objects that werent deconstructed properly
        if (walker::constructed_count != 0 || particle::constructed_count != 0)
        error_file << "PID: " << pid << " un-deleted objects:\n"
                   << "  Walkers   : " << walker::constructed_count   << "\n"
                   << "  Particles : " << particle::constructed_count << "\n";

    error_file.close();
    MPI_Finalize();
}

void params :: flush()
{
    // Flush all of the output files to disk
    error_file.flush();
    progress_file.flush();
    evolution_file.flush();
    wavefunction_file.flush();
}

double params :: time()
{
    // Return the time in seconds since startup
    return double(clock()-start_clock)/double(CLOCKS_PER_SEC);
}