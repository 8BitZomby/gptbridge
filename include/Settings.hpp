#ifndef GPTB_SETTINGS_HPP
#define GPTB_SETTINGS_HPP


/**
 * PushMode
 * Determines how `gptb push` combines newly selected terminal interactions
 * with context already stored for the current session.
 */
enum class PushMode {
    Append,
    Replace
};


/**
 * getPushMode()
 * Returns the globally configured push mode.
 * Append is used when no explicit setting has been saved yet.
 */
PushMode getPushMode();


/**
 * setPushMode()
 * Persists the global mode used by future `gptb push` operations.
 */
void setPushMode(PushMode mode);


#endif
