#ifndef GPTB_CONTEXT_COMMANDS_HPP
#define GPTB_CONTEXT_COMMANDS_HPP


/**
 * handlePushCommand()
 * Handles terminal-I/O selection and push-policy arguments
 */
int handlePushCommand(int argc, char* argv[]);


/**
 * handleShowCommand()
 * Displays terminal I/O currently stored as context
 */
int handleShowCommand(int argc, char* argv[]);


/**
 * handleClearCommand()
 * Clears terminal I/O currently stored as context
 */
int handleClearCommand(int argc, char* argv[]);


/**
 * handleRemoveCommand()
 * Removes selected terminal interactions from stored context
 */
int handleRemoveCommand(int argc, char* argv[]);


#endif
