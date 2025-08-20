/**
 * @file simd_simulation.cpp
 * @brief Example of SIMD-optimized particle simulation using shared memory
 * @author Alex Towell
 * 
 * Demonstrates:
 * - SIMD operations on shared memory arrays
 * - Structure of Arrays (SoA) layout for better vectorization
 * - Cache-friendly access patterns
 * - Performance comparison vs scalar operations
 */

#include <iostream>
#include <chrono>
#include <random>
#include <iomanip>
#include "posix_shm.h"
#include "shm_array.h"
#include "shm_simd_utils.h"

using namespace std::chrono;

// Number of particles in simulation
constexpr size_t NUM_PARTICLES = 100000;
constexpr size_t ITERATIONS = 100;

/**
 * Structure of Arrays for particle data
 * Better for SIMD than Array of Structures
 */
struct ParticleSystemSoA {
    shm_array<float> x, y, z;      // Position
    shm_array<float> vx, vy, vz;   // Velocity
    shm_array<float> ax, ay, az;   // Acceleration
    shm_array<float> mass;         // Mass
    
    ParticleSystemSoA(posix_shm& shm, size_t count)
        : x(shm, "pos_x", count), y(shm, "pos_y", count), z(shm, "pos_z", count),
          vx(shm, "vel_x", count), vy(shm, "vel_y", count), vz(shm, "vel_z", count),
          ax(shm, "acc_x", count), ay(shm, "acc_y", count), az(shm, "acc_z", count),
          mass(shm, "mass", count) {}
};

// Initialize particles with random values
void initialize_particles(ParticleSystemSoA& particles) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> pos_dist(-100.0f, 100.0f);
    std::uniform_real_distribution<float> vel_dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> mass_dist(0.1f, 10.0f);
    
    for (size_t i = 0; i < NUM_PARTICLES; ++i) {
        particles.x[i] = pos_dist(gen);
        particles.y[i] = pos_dist(gen);
        particles.z[i] = pos_dist(gen);
        
        particles.vx[i] = vel_dist(gen);
        particles.vy[i] = vel_dist(gen);
        particles.vz[i] = vel_dist(gen);
        
        particles.ax[i] = 0.0f;
        particles.ay[i] = 0.0f;
        particles.az[i] = -9.81f;  // Gravity
        
        particles.mass[i] = mass_dist(gen);
    }
}

// Scalar version of physics update
void update_physics_scalar(ParticleSystemSoA& particles, float dt) {
    for (size_t i = 0; i < NUM_PARTICLES; ++i) {
        // Update velocity: v = v + a * dt
        particles.vx[i] += particles.ax[i] * dt;
        particles.vy[i] += particles.ay[i] * dt;
        particles.vz[i] += particles.az[i] * dt;
        
        // Update position: p = p + v * dt
        particles.x[i] += particles.vx[i] * dt;
        particles.y[i] += particles.vy[i] * dt;
        particles.z[i] += particles.vz[i] * dt;
        
        // Simple boundary collision (bounce)
        if (particles.z[i] < 0.0f) {
            particles.z[i] = 0.0f;
            particles.vz[i] = -particles.vz[i] * 0.8f;  // Energy loss
        }
    }
}

// SIMD version of physics update
void update_physics_simd(ParticleSystemSoA& particles, float dt) {
    // Prepare dt vectors
    __m256 dt_vec = _mm256_set1_ps(dt);
    __m256 zero_vec = _mm256_setzero_ps();
    __m256 bounce_factor = _mm256_set1_ps(0.8f);
    
    size_t simd_count = NUM_PARTICLES & ~7;  // Process 8 at a time
    
    for (size_t i = 0; i < simd_count; i += 8) {
        // Load current values
        __m256 vx = _mm256_loadu_ps(&particles.vx[i]);
        __m256 vy = _mm256_loadu_ps(&particles.vy[i]);
        __m256 vz = _mm256_loadu_ps(&particles.vz[i]);
        
        __m256 ax = _mm256_loadu_ps(&particles.ax[i]);
        __m256 ay = _mm256_loadu_ps(&particles.ay[i]);
        __m256 az = _mm256_loadu_ps(&particles.az[i]);
        
        __m256 px = _mm256_loadu_ps(&particles.x[i]);
        __m256 py = _mm256_loadu_ps(&particles.y[i]);
        __m256 pz = _mm256_loadu_ps(&particles.z[i]);
        
        // Update velocity: v = v + a * dt (using FMA)
        vx = _mm256_fmadd_ps(ax, dt_vec, vx);
        vy = _mm256_fmadd_ps(ay, dt_vec, vy);
        vz = _mm256_fmadd_ps(az, dt_vec, vz);
        
        // Update position: p = p + v * dt
        px = _mm256_fmadd_ps(vx, dt_vec, px);
        py = _mm256_fmadd_ps(vy, dt_vec, py);
        pz = _mm256_fmadd_ps(vz, dt_vec, pz);
        
        // Boundary collision for z
        __m256 below_ground = _mm256_cmp_ps(pz, zero_vec, _CMP_LT_OQ);
        pz = _mm256_max_ps(pz, zero_vec);  // Clamp to ground
        
        // Reverse and dampen velocity for particles that hit ground
        __m256 neg_vz = _mm256_mul_ps(vz, _mm256_set1_ps(-1.0f));
        __m256 damped_vz = _mm256_mul_ps(neg_vz, bounce_factor);
        vz = _mm256_blendv_ps(vz, damped_vz, below_ground);
        
        // Store results
        _mm256_storeu_ps(&particles.vx[i], vx);
        _mm256_storeu_ps(&particles.vy[i], vy);
        _mm256_storeu_ps(&particles.vz[i], vz);
        
        _mm256_storeu_ps(&particles.x[i], px);
        _mm256_storeu_ps(&particles.y[i], py);
        _mm256_storeu_ps(&particles.z[i], pz);
    }
    
    // Handle remainder with scalar code
    for (size_t i = simd_count; i < NUM_PARTICLES; ++i) {
        particles.vx[i] += particles.ax[i] * dt;
        particles.vy[i] += particles.ay[i] * dt;
        particles.vz[i] += particles.az[i] * dt;
        
        particles.x[i] += particles.vx[i] * dt;
        particles.y[i] += particles.vy[i] * dt;
        particles.z[i] += particles.vz[i] * dt;
        
        if (particles.z[i] < 0.0f) {
            particles.z[i] = 0.0f;
            particles.vz[i] = -particles.vz[i] * 0.8f;
        }
    }
}

// Calculate total kinetic energy (demonstrates reduction)
float calculate_kinetic_energy_scalar(ParticleSystemSoA& particles) {
    float total_ke = 0.0f;
    
    for (size_t i = 0; i < NUM_PARTICLES; ++i) {
        float v_squared = particles.vx[i] * particles.vx[i] +
                         particles.vy[i] * particles.vy[i] +
                         particles.vz[i] * particles.vz[i];
        total_ke += 0.5f * particles.mass[i] * v_squared;
    }
    
    return total_ke;
}

// SIMD version using helper functions
float calculate_kinetic_energy_simd(ParticleSystemSoA& particles) {
    // Create temporary arrays for v²
    shm_array<float> v_squared(particles.x.shm, "v_squared_temp", NUM_PARTICLES);
    
    // Calculate v² = vx² + vy² + vz² using SIMD
    size_t simd_count = NUM_PARTICLES & ~7;
    
    for (size_t i = 0; i < simd_count; i += 8) {
        __m256 vx = _mm256_loadu_ps(&particles.vx[i]);
        __m256 vy = _mm256_loadu_ps(&particles.vy[i]);
        __m256 vz = _mm256_loadu_ps(&particles.vz[i]);
        
        __m256 vx2 = _mm256_mul_ps(vx, vx);
        __m256 vy2 = _mm256_mul_ps(vy, vy);
        __m256 vz2 = _mm256_mul_ps(vz, vz);
        
        __m256 v2 = _mm256_add_ps(vx2, _mm256_add_ps(vy2, vz2));
        _mm256_storeu_ps(&v_squared[i], v2);
    }
    
    // Handle remainder
    for (size_t i = simd_count; i < NUM_PARTICLES; ++i) {
        v_squared[i] = particles.vx[i] * particles.vx[i] +
                      particles.vy[i] * particles.vy[i] +
                      particles.vz[i] * particles.vz[i];
    }
    
    // Scale by 0.5 * mass
    shm_simd::scale_floats(v_squared.data(), NUM_PARTICLES, 0.5f);
    
    // Use SIMD dot product to sum (mass * v²/2)
    return shm_simd::dot_product(particles.mass.data(), v_squared.data(), NUM_PARTICLES);
}

int main() {
    try {
        // Create shared memory segment
        size_t shm_size = 20 * NUM_PARTICLES * sizeof(float);  // Space for all arrays
        posix_shm shm("simd_simulation", shm_size);
        
        std::cout << "=== SIMD Particle Simulation ===" << std::endl;
        std::cout << "Particles: " << NUM_PARTICLES << std::endl;
        std::cout << "Iterations: " << ITERATIONS << std::endl;
        std::cout << std::endl;
        
        // Create particle system
        ParticleSystemSoA particles(shm, NUM_PARTICLES);
        initialize_particles(particles);
        
        const float dt = 0.01f;  // Time step
        
        // Benchmark scalar version
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            update_physics_scalar(particles, dt);
        }
        float ke_scalar = calculate_kinetic_energy_scalar(particles);
        auto end = high_resolution_clock::now();
        auto scalar_time = duration_cast<microseconds>(end - start).count();
        
        // Reset particles
        initialize_particles(particles);
        
        // Benchmark SIMD version
        start = high_resolution_clock::now();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            update_physics_simd(particles, dt);
        }
        float ke_simd = calculate_kinetic_energy_simd(particles);
        end = high_resolution_clock::now();
        auto simd_time = duration_cast<microseconds>(end - start).count();
        
        // Results
        std::cout << "Performance Results:" << std::endl;
        std::cout << "-------------------" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Scalar version: " << scalar_time << " µs" << std::endl;
        std::cout << "SIMD version:   " << simd_time << " µs" << std::endl;
        std::cout << "Speedup:        " << (float)scalar_time / simd_time << "x" << std::endl;
        std::cout << std::endl;
        
        std::cout << "Final kinetic energy:" << std::endl;
        std::cout << "Scalar: " << ke_scalar << " J" << std::endl;
        std::cout << "SIMD:   " << ke_simd << " J" << std::endl;
        std::cout << "Difference: " << std::abs(ke_scalar - ke_simd) << " J" << std::endl;
        std::cout << std::endl;
        
        // Demonstrate other SIMD operations
        std::cout << "SIMD Helper Functions:" << std::endl;
        std::cout << "---------------------" << std::endl;
        
        shm_simd::SimdArray vx_simd(particles.vx);
        std::cout << "Min velocity X: " << vx_simd.min() << " m/s" << std::endl;
        std::cout << "Max velocity X: " << vx_simd.max() << " m/s" << std::endl;
        std::cout << "Sum velocity X: " << vx_simd.sum() << " m/s" << std::endl;
        
        // Check alignment
        std::cout << std::endl;
        std::cout << "Memory Alignment:" << std::endl;
        std::cout << "----------------" << std::endl;
        std::cout << "Position X aligned to 32: " 
                  << shm_simd::is_aligned(particles.x.data(), 32) << std::endl;
        std::cout << "Velocity X aligned to 32: " 
                  << shm_simd::is_aligned(particles.vx.data(), 32) << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}