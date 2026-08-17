#ifndef GPTB_PROJECT_COMMANDS_HPP
#define GPTB_PROJECT_COMMANDS_HPP


/**
 * Handles "gptb add project <name> <path>".
 */
int handleAddProjectCommand(int argc, char* argv[]);


/**
 * Handles "gptb init <path> <project-name>".
 */
int handleInitProjectCommand(int argc, char* argv[]);


/**
 * Handles "gptb use <project|.>".
 */
int handleUseProjectCommand(int argc, char* argv[]);


#endif
