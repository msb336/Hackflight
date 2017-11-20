/*
   sim.hpp: Hackflight Board class implementation for flight simulators

   Simulates quadcopter physics

   Copyright (C) Simon D. Levy 2017

   This file is part of Hackflight.

   Hackflight is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Hackflight is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with Hackflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <board.hpp>
#include <debug.hpp>


namespace hf {

    class SimBoard : public Board {

        private:

            // A true constant!
            const float GRAVITY = 9.80665;

            // Scales up thrust to radians per second (substitutes for mass, torque, etc.)
            const float THRUST_SCALE = 5.f;

            // Controls "snappiness" of response
            const float VELOCITY_ROTATE_SCALE    = 1.75;
            const float VELOCITY_TRANSLATE_SCALE = 0.05f;

            // Time constant, set by simulator
            float _deltaSeconds;

            // Private state variables
            uint64_t _micros;
            float _accel[3];          // Gs
            float _gyro[3];           // radians per second
            float _angles[3];         // radians
            float _baroPressure;      // Pascals (milllibars)
            float _linearSpeeds[3];   // meters per second forward, lateral, vertical
            float _motors[4];         // arbitrary in [0,1]
            float _altitude;          // meters
            bool _flying;

        // methods called by simulator
        public:

            SimBoard(float deltaSeconds)
            {
                _deltaSeconds = deltaSeconds;

                for (uint8_t k=0; k<3; ++k) {
                    _angles[k] = 0;
                    _gyro[k] = 0;
                    _accel[k] = 0;
                    _linearSpeeds[k] = 0;
                }

                for (uint8_t k=0; k<4; ++k) {
                    _motors[k] = 0;
                }

                _flying = false;
                _altitude = 0;
                _baroPressure = 0;
                _micros = 0;
            }

            void getState(float angularSpeeds[3], float linearSpeeds[3], float motors[4], bool & flying)
            {
                for (uint8_t k=0; k<3; ++k) {
                    angularSpeeds[k] = _gyro[k];
                    linearSpeeds[k] = _linearSpeeds[k];
                }

                for (uint8_t k=0; k<4; ++k) {
                    motors[k] = _motors[k];
                }

                flying = _flying;
            }

        // methods called by Hackflight
        public:

            void init(Config& config)
            {
                // Loop timing overrides
                config.loop.imuLoopMicro = 1e6 * _deltaSeconds;
            }

            bool skipArming(void)
            {
                return true;
            }

            void getImu(float eulerAnglesRadians[3], float gyroRadiansPerSecond[3])
            {
                for (uint8_t k=0; k<3; ++k) {
                    eulerAnglesRadians[k] = _angles[k];
                    gyroRadiansPerSecond[k] = -_gyro[k];
                }

                // Sync physics update to IMU acquisition by Hackflight
                updatePhysics();
            }

            uint64_t getMicros()
            {
                return _micros;
            }

            void writeMotor(uint8_t index, float value)
            {
                _motors[index] = value;
            }

            void delayMilliseconds(uint32_t msec)
            {
            }

            virtual bool extrasHaveBaro(void)
            {
                return true;
            }

            virtual float extrasGetBaroPressure(void)
            {
                return _baroPressure;
            }

            virtual void extrasImuGetAccel(float accelGs[3])
            {
                for (uint8_t k = 0; k<3; ++k) {
                    accelGs[k] = 1.f; // XXX
                }
            }

        // private methods
        private:

            void updatePhysics(void)
            {
                // Compute body-frame roll, pitch, yaw velocities based on differences between motors
                _gyro[0] = motorsToAngularVelocity(2, 3, 0, 1);
                _gyro[1] = motorsToAngularVelocity(1, 3, 0, 2); 
                _gyro[2] = motorsToAngularVelocity(1, 2, 0, 3); 

                // Overall thrust vector, scaled by arbitrary constant for realism
                float thrust = THRUST_SCALE * (_motors[0] + _motors[1] + _motors[2] + _motors[3]);

                // Compute right column of of R matrix converting body coordinates to world coordinates.
                // See page 7 of http://repository.upenn.edu/cgi/viewcontent.cgi?article=1705&context=edissertations.
                float phi   = _angles[0];
                float theta = _angles[1];
                float psi   = _angles[2];
                float r02 = cos(psi)*sin(theta) + cos(theta)*sin(phi)*sin(psi);
                float r12 = sin(psi)*sin(theta) - cos(psi)*cos(theta)*sin(phi);
                float r22 = cos(phi)*cos(theta);

                // Overall vertical force = thrust - gravity
                // We first multiply by the sign of the vertical world coordinate direction, because simulator
                // will upside-down vehicle rise on negative velocity.
                float lift = (r22 < 0 ? -1 : +1) * (r22*thrust - GRAVITY);

                // Once there's enough lift, we're flying
                if (lift > 0) {
                    _flying = true;
                }

                if (_flying) {

                    // Integrate vertical force to get vertical speed
                    _linearSpeeds[2] += (lift * _deltaSeconds);

                    // To get forward and lateral speeds, integrate thrust along world coordinates
                    _linearSpeeds[0] += thrust * VELOCITY_TRANSLATE_SCALE * r02;
                    _linearSpeeds[1] -= thrust * VELOCITY_TRANSLATE_SCALE * r12;
                }

                // Integrate vertical speed to get altitude
                _altitude += _linearSpeeds[2] * _deltaSeconds;

                // Track time
                _micros += 1e6 * _deltaSeconds;

                // Update state
                for (int k=0; k<3; ++k) {
                    _angles[k] += ((k==1) ? -1 : +1) * _gyro[k] * _deltaSeconds; // negate pitch
                }

                // Convert lift to Gs
                float accelZ = lift / -GRAVITY + 1;

                /*
                // Estimate G forces on accelerometer using Equations 2, 6-8 in
                // https://www.nxp.com/docs/en/application-note/AN3461.pdf
                float phi   = angles[0]; // roll
                float theta = angles[1]; // pitch
                accel[0] = verticalAcceleration * -sin(theta);              // accel X   
                accel[1] = verticalAcceleration *  cos(theta) * sin(phi);   // accel Y   
                accel[2] = verticalAcceleration *  cos(theta) * cos(phi);   // accel Z   
                 */

                // Convert vehicle's Z coordinate in meters to barometric pressure in Pascals (millibars)
                // At low altitudes above the sea level, the pressure decreases by about 1200 Pa for every 100 meters
                // (See https://en.wikipedia.org/wiki/Atmospheric_pressure#Altitude_variation)
                _baroPressure = 1000 * (101.325 - 1.2 * _altitude / 100);
            }

            float motorsToAngularVelocity(int a, int b, int c, int d)
            {
                return VELOCITY_ROTATE_SCALE * ((_motors[a] + _motors[b]) - (_motors[c] + _motors[d]));
            }

    }; // class SimBoard

} // namespace hf
