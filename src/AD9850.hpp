#pragma once

#include <pico/stdlib.h>
#include <stdio.h>
#include <map>
#include <string>

// For information on the Raspberry Pi Pico GPIOs, see
// https://raspberrypi.github.io/pico-sdk-doxygen/group__hardware__gpio.html
//
namespace
{
    class AD9850
    {
    public:
        /**
         * @brief  Constructor
         * @param  osc_hz  Oscillator frequency, in Hz.
         * @param  w_clk   Word Load Clock.  Used to load frequency/phase/
         *                 control words.
         * @param  fq_ud   Frequency Update.  The DDS will update to the 
         *                 frequency (or phase) loaded in the data input 
         *                 register on the rising edge.
         * @param  data    Input pin for the serial data word.
         * @param  reset   Master reset function.  Active hi.
         */
        AD9850(uint32_t osc_hz, uint w_clk, uint fq_ud, uint data, uint reset)
            : osc_hz_(osc_hz)
            , w_clk_(w_clk)
            , fq_ud_(fq_ud)
            , data_(data)
            , reset_(reset)
            , frequency_hz_(0.0)
            , phase_deg_(0.0)
            , enable_out_(false)
            , frequency_hz_t_(frequency_hz_)
            , phase_deg_t_(phase_deg_)
            , enable_out_t_(enable_out_)            
            , frequency_register_(0x00)
            , phase_register_(0x00)
        {
            // Initialize the GPIO to communicate with the chip.
            //
            gpio_init(w_clk_);
            gpio_init(fq_ud_);
            gpio_init(data_);
            gpio_init(reset_);

            gpio_set_dir(w_clk_, GPIO_OUT);
            gpio_set_dir(fq_ud_, GPIO_OUT);
            gpio_set_dir(data_,  GPIO_OUT);
            gpio_set_dir(reset_, GPIO_OUT);

            pulse(reset_);
            pulse(w_clk_);
            pulse(fq_ud_);

            program_dds(frequency_register_, phase_register_, enable_out_);
        }

        /**
         * @brief  Set the sig gen frequency.
         * @param  frequency  Signal generator frequency, in Hz.
         * @note   Does not take effect until the commit method is called.
         */
        auto set_frequency(uint32_t frequency) -> void
        {
            frequency_hz_t_ = frequency;            
        }

        /**
         * @brief  Set the sig gen phase.
         * @param  phase  Signal generator phase, in .01 deg increments.
         * @note   Putting the phase in increments of .01 deg means multiply
         *         the phase by 100 before passing it into the method.
         *         So, for example, a phase of 22.5 deg is passed in as 2250.
         * @note   Does not take effect until the commit method is called.
         */
        auto set_phase(uint32_t phase) -> void
        {
            phase_deg_t_ = phase;
        }

        /**
         * @brief  Enable/disable the sig gen output.
         * @param  enable  Enable output if true, otherewise disable output.
         * @note   Does not take effect until the commit method is called.
         */
        auto enable_out(bool enable) -> void
        {
            enable_out_t_ = enable;
        }

        /**
         * @brief  Return the sig gen frequency, in Hz.
         */
        auto get_frequency() -> uint32_t
        {
            return frequency_hz_;
        }

        /**
         * @brief  Return sign gen phase, in deg.
         */
        auto get_phase() -> uint32_t
        {
            return phase_deg_;
        }

        /**
         * @brief  Return the output enabled flag.
         */
        auto get_enabled() -> bool
        {
            return enable_out_;
        }

        /**
         * @brief  Program the DDS with the current state values.
         */
        auto commit() -> void
        {
            // The DDS only does phase in increments of 22.5 deg. so the
            // actual phase may not correspond to the requested phase.  This
            // is taken into account when calculating the phase register.
            // 
            frequency_register_ = calculate_frequency_register(osc_hz_, frequency_hz_t_);
            frequency_hz_ = frequency_hz_t_;

            phase_register_ = calculate_phase_register(phase_deg_t_);
            phase_deg_ = phase_register_ * PHASE_INC;

            enable_out_ = enable_out_t_;

            program_dds(frequency_register_, phase_register_, enable_out_);
        }

        static const uint32_t OSC_HZ = 125000000;

    private:

        /**
         * @brief  Calculate the frequency register value to be sent to the DDS.
         * @param  osc_hz           DDS oscillator value, in Hz.
         * @param  frequency_hz     Desired DDS frequency.
         */
        auto calculate_frequency_register(
            uint32_t osc_hz,
            uint32_t frequency_hz) -> uint32_t
        {
            // See the AD9850 data sheet for the equation.
            //
            uint64_t frequency_reg = static_cast<uint64_t>(frequency_hz) << 32;
            return static_cast<uint32_t>(frequency_reg / osc_hz);
        }
        
        /**
         * @brief  Calculate the phase register value that corresponds
         *         to the requested phase.
         * @param  phase  Requested phase, in multiple of .01 deg.
         */
        auto calculate_phase_register(uint32_t phase) -> uint32_t
        {
            // You're looking to calculate the phase in increments of
            // 11.25 deg.  Since the incoming phase value is in 
            // increments of .01 deg, start by dividing by the
            // phase increment to get a quotient and remainder.
            //
            int quotient  = phase / PHASE_INC;
            int remainder = phase % PHASE_INC;

            // If the remainder is > 1/2 the phase increment you're 
            // closer to the the next higher value so round it up.
            // Since we're dealing with integers, rather that compare
            // the remainder to 1/2 the phase increment, compare 
            // 2 * remainder to the phase increment.
            //
            if (2 * remainder > PHASE_INC)
                quotient += 1;

            // Now calculate and return the actual phase value that
            // will be set.
            //
            return quotient % PHASE_MAX;
        }

        /**
         * @brief  Send the frequency, phase, and enabled values to the DDS.
         * @param  frequency_register  Frequency portion of the word to be sent to the DDS.
         * @param  phase_register      Phase portion of the word to be sent to the DDS.
         */
        auto program_dds(
            uint32_t frequency_register,
            uint32_t phase_register,
            bool enable_out) -> void
        {
            // First the frequency register.
            // Word is 32 bits, sent LSB first.
            //
            uint32_t mask = 0x01;
            for (size_t bit = 0; bit < 32; ++bit, mask = mask << 1)
            {
                uint bit_value = (frequency_register & mask) == 0 ? GPIO_LO : GPIO_HI;
                write_data(data_, bit_value);
                pulse(w_clk_);
            }

            // Two control bits, both set to zero.
            //
            {
                write_data(data_, GPIO_LO);
                pulse(w_clk_);
                write_data(data_, GPIO_LO);
                pulse(w_clk_);
            }

            // Power down bit.
            //
            {
                uint bit_value = (enable_out) ? POWER_UP : POWER_DOWN;
                write_data(data_, bit_value);
                pulse(w_clk_);
            }

            // Finally, the phase value, LSB first.
            //
            mask = 0x01;
            for (size_t bit = 0; bit < 5; ++bit, mask = mask << 1)
            {
                uint bit_value = (phase_register & mask) == 0 ? GPIO_LO : GPIO_HI;
                write_data(data_, bit_value);
                pulse(w_clk_);
            }

            // Pulse the frequency update pin to load the frequency.
            //
            pulse(fq_ud_);
        }
        
        /**
         * @brief  Pulse the given pin.
         * @param  pin  GPIO to pulse
         */
        auto pulse(uint pin) -> void
        {
            gpio_put(pin, GPIO_HI);
            gpio_put(pin, GPIO_LO);
        }

        /**
         * @brief  Write data to a pin with a delay
         * @param  pin    Pin to where the data is to be written.
         * @param  level  Level to write to the pin.
         */
        auto write_data(uint pin, uint level) -> void
        {
            gpio_put(pin, level);
        }

        // Local values.
        //
        static const uint GPIO_HI = 1;
        static const uint GPIO_LO = 0;

        static const uint POWER_DOWN = 1;
        static const uint POWER_UP   = 0;

        static const uint PHASE_INC = 1125;
        static const uint PHASE_MAX = 32;

        uint32_t osc_hz_;               // See constructor for these value definitions.
        uint w_clk_;
        uint fq_ud_;
        uint data_;
        uint reset_;

        uint32_t frequency_hz_;         // Current signal generator frequency, in  Hz.
        uint32_t phase_deg_;            // Current signal generator phase, in deg.
        bool enable_out_;               // Output enabled if true, otherwise disabled.

        uint32_t frequency_hz_t_;       // Temporary values before commit.
        uint32_t phase_deg_t_;
        bool enable_out_t_;     

        uint32_t frequency_register_;
        uint32_t phase_register_;
    };
}