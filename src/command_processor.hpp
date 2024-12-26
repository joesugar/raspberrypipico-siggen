#pragma once

#include <vector>
#include <iostream>
#include <optional>

#include <stdio.h>
#include <string.h>

#include "tiny-json.h"

namespace
{
    // Define the structure used to contain a DDS command.
    //
    using command_t = struct {
        int command_number = 0x00;
        std::optional<uint32_t> frequency_hz = std::nullopt;
        std::optional<uint32_t> phase_deg = std::nullopt;
        std::optional<bool> enable_out = std::nullopt;
        std::optional<std::string> error = std::nullopt;
    };

    // Now the command receiver class.
    //
    class CommandProcessor
    {
    public:
        /**
         * @brief  Class constructor
         */
        CommandProcessor() :
            command_buffer_index_(0),
            show_prompt_(false),
            crlf_(false)
        {
            // Zero out the command buffer.
            //
            reset_command_buffer();
        }

        /**
         * @brief  Return a flag indicate if a command is available.
         */
        auto command_is_available() -> bool
        {
            return commands_.size() > 0;
        }

        /**
         * @brief  Return the number of commands in the fifo.
         */
        auto number_of_commands() -> int
        {
            return commands_.size();
        }

        /**
         * @brief  Return the command at the top of the command fifo.
         */
        auto get_command() -> command_t
        {
            command_t command {};

            if (command_is_available())
            {
                command = commands_[0];
                commands_.erase(commands_.begin());
            }

            return command;
        }

        /**
         * @brief  Method to execute instructions that look for
         *         incoming commands.
         */
        auto loop() -> void
        {
            if (show_prompt_)
            {
                display_prompt();       // Displays the prompt.
                show_prompt(false);     // Resets the flag.
            }

            // Get the character from stdio.  If it's a timeout 
            // you can just leave the method.
            //
            int character = stdio_getchar_timeout_us(0);
            if (character == PICO_ERROR_TIMEOUT)
                return;

            // If you get a LF right after a CR ignore it.  We
            // map CR to LF below and don't want two in a row.
            //
            if ((character == '\n') && (crlf_))
            {
                crlf_ = false;
                return;
            }

            // We're mapping CR and LF to 0x00 since they are 
            // considered to be line terminators.
            //
            crlf_ = (character == '\r');
            if ((character == '\r') || (character == '\n'))
            {
                character = 0x00;
            }

            // At this point we should have handled any special
            // characters and/or character mapping.
            //
            if (character == 0x00)
            {
                // The incoming character is a line terminator so
                // process the command.  Once you've processed the 
                // command be sure to reset the buffer and command index.
                //
                if (command_buffer_index_ > 0)
                {
                    add_command_to_fifo();
                    reset_command_buffer();
                }
                reflect(character);
                show_prompt(true);
            }
            else if (command_buffer_index_ >= MAX_COMMAND_LEN)
            {
                // There's no room in the command buffer so there's
                // nothing to do.  Just ignore the incoming character.
            }
            else if ((character >= 32) && (character <= 128))
            {
                // Reflect the character back to provide feedback and
                // put it in the buffer.
                //
                reflect(character);
                command_buffer_[command_buffer_index_++] = static_cast<char>(character);
            }
        }

    private:

        static const int COMMAND_BUFFER_LEN = 1024;
        static const int MAX_COMMAND_LEN = COMMAND_BUFFER_LEN - 1;
        static const int MAX_JSON_DEPTH = 8;

        /**
         * @brief  Send a single character out the stdio.
         * @param  character  Character to be sent.
         */
        auto reflect(int character) -> void
        {
            std::cout << ((character == 0x00) ? '\n' : static_cast<char>(character));
            std::cout << std::flush;
        }

        /**
         * @brief  Enable/disable showing the prompt.
         * @note   This function only exists to help wtih code
         *         readability.  Fully expect the compiler to
         *         inline it.
         */
        auto show_prompt(bool flag) -> void 
        {
            show_prompt_ = flag;
        }

        /**
         * @brief  Display the command line prompt.
         */
        auto display_prompt() -> void
        {
            std::cout << "$ " << std::flush;
        }

        /**
         * @brief  Pull command from the buffer and put it on the fifo
         */
        auto add_command_to_fifo() -> void
        {
            std::optional<command_t> command = parse_json_command_buffer();
            commands_.push_back(command.value());
        }

        /**
         * @brief  Reinitialize the command buffer.  Sets all values to 0x00
         *         and the length to zero.
         */
        auto reset_command_buffer() -> void
        {
            memset(command_buffer_, 0x00, COMMAND_BUFFER_LEN);
            command_buffer_index_ = 0;
        }

        /**
         * @brief  Parse the command buffer to retrieve a command.
         */
        auto parse_json_command_buffer() -> std::optional<command_t>
        {
            std::string command(command_buffer_);
            command_t command_struct;

            // Convert incoming command buffer to a json object.  
            // If conversion fails just return the default 
            // command structure.
            //
            json_t mem[MAX_JSON_DEPTH];
            json_t const* json = json_create( command_buffer_, mem, sizeof(mem) / sizeof(*mem) );
            if (!json)
            {
                command_struct.error = 
                    std::make_optional("Error creating json from command buffer");
                return command_struct;
            }

            // The command number is a required field.
            //
            json_t const* command_number = json_getProperty(json, "command_number");
            if (!command_number || (JSON_INTEGER != json_getType(command_number)))
            {
                command_struct.error =
                    std::make_optional("Error parsing command number");
                return command_struct;
            }
            command_struct.command_number =
                static_cast<uint32_t>(json_getInteger(command_number));

            // Now pull out the properties of interest and return the completed
            // command structure.
            //
            json_t const* enable_out = json_getProperty(json, "enable_out");
            if (enable_out)
            {
                if (JSON_BOOLEAN != json_getType( enable_out ))
                {
                    command_struct.error =
                        std::make_optional("Error parsing enable flag.");
                    return command_struct;
                }
                command_struct.enable_out =
                    std::make_optional(json_getBoolean( enable_out ));
            }

            json_t const* frequency_hz = json_getProperty(json, "frequency");
            if (frequency_hz)
            {
                if (JSON_INTEGER != json_getType( frequency_hz ))
                {
                    command_struct.error =
                        std::make_optional("Error parsing frequency.");
                    return command_struct;
                }
                command_struct.frequency_hz = 
                    std::make_optional(static_cast<uint32_t>(json_getInteger( frequency_hz )));
            }

            json_t const* phase_deg = json_getProperty(json, "phase");
            if (phase_deg)
            {
                if (JSON_INTEGER != json_getType( phase_deg ))
                {
                    command_struct.error =
                        std::make_optional("Error parsing phase");
                    return command_struct;
                }
                command_struct.phase_deg = 
                    std::make_optional(static_cast<uint32_t>(json_getInteger( phase_deg )));
            }

            return command_struct;
        }


        // FIFO for storing received commands.
        //
        std::vector<command_t> commands_ {  };

        // Buffer used to store incoming chaacters.
        //
        char command_buffer_[COMMAND_BUFFER_LEN];
        int command_buffer_index_;

        // Flags used to control local state.
        //
        bool show_prompt_;
        bool crlf_;
    };
}