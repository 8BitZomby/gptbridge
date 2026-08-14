# gptbridge zsh integration
#
# Loaded only inside a shell launched by `gptb capture`.
# Installs shell lifecycle hooks that report command-start and command-finish
# metadata to the parent process through gptbridge's private control protocol.
#
# GPTB_SESSION_NONCE is supplied by the parent before the shell is executed,
# allowing emitted control frames to be authenticated as belonging to the
# current capture session.

# Do nothing in ordinary shells. PtyCaptureBackend sets this variable only
# for shells launched inside a gptbridge capture session.
# If the nonce is empty or unset, stop loading the script immediately
# ":-" prevents an unset variable from causing trouble
[[ -n "${GPTB_SESSION_NONCE:-}" ]] || return

# The captured shell must know the exact gptb executable that launched it.
# Without this path, internal shell-event reporting cannot be trusted.
[[ -n "${GPTB_EXECUTABLE:-}" ]] || return

# Load zsh's supported hook-registration helper so gptbridge can add its oen
# preexec/precmd callbacks without replacing hooks installed by other tools.
# This command loads zsh's built-in/contributed hook-registration helper
# rather than defining global preexec() and precmd() functions directly.
autoload -Uz add-zsh-hook

# Counter used to assign a unique interaction ID to each command in this shell.
typeset -gi GPTB_INTERACTION_COUNTER=0

# ID of the command currently executing. Empty means no command is active.
typeset -g GPTB_ACTIVE_INTERACTION_ID=""

# Called by zsh immediately before an interactive command begins executing.
# $1 contains the command text as entered by the user in the interactive shell.
_gptb_preexec() {
    # Allocate a new ID and remember it so precmd can later report completion
    # for the same command.
    (( ++GPTB_INTERACTION_COUNTER ))
    GPTB_ACTIVE_INTERACTION_ID="$GPTB_INTERACTION_COUNTER"

    # Report the command start through gptbridge's internal CLI endpoint.
    # Quoting each expansion preserves spaces and shell-special characters as
    # part of a single argument rather than allowing them to be reinterpreted.
    "$GPTB_EXECUTABLE" shell-event started \
        "$GPTB_ACTIVE_INTERACTION_ID" \
        "$1" \
        "$PWD"

    # TEMP ADDITION -----------------
    print -r -- "START id=$GPTB_ACTIVE_INTERACTION_ID command=$1" >> /tmp/gptb-hook-test.log

    # Do not let an internal gptbridge report failure stop later preexec hooks
    return 0
}

# Called by zsh before displaying the next prompt after a command finishes.
_gptb_precmd() {
    # Save the user's command exit status immediately. Any command executed
    # below, including gptb itself, would otherwise replace the value in $?.
    local exit_code=$?

    # precmd also runs when the first prompt is displayed, before any command
    # has started. In that case there is no interaction to finish.
    if [[ -z "$GPTB_ACTIVE_INTERACTION_ID" ]]; then
        return 0
    fi

    # Report completion using the same interaction ID assigned by preexec.
    "$GPTB_EXECUTABLE" shell-event finished \
        "$GPTB_ACTIVE_INTERACTION_ID" \
        "$exit_code"

    # TEMP ADDITION -------------
    print -r -- "FINISH id=$GPTB_ACTIVE_INTERACTION_ID exit=$exit_code" >> /tmp/gptb-hook-test.log

    # The interaction is complete. Clearing the ID prevents a later prompt
    # from accidentally reporting the same command a second time.
    GPTB_ACTIVE_INTERACTION_ID=""

    # The command's original exit status has already been captured and reported.
    # Return success so gptbridge does not prevent later precmd hooks from running.
    return 0
}

# Called when zsh itself is about to exit. This closes an interaction such as
# the `exit` command, which runs preexec but never reaches another precmd hook.
_gptb_zshexit() {
    # If no command is active, there is nothing for gptbridge to finish.
    if [[ -z "$GPTB_ACTIVE_INTERACTION_ID" ]]; then
        return 0
    fi

    # $? at shell-exit time contains the status with which zsh is exiting.
    local exit_code=$?

    # Report completion for the interaction that was started by preexec
    "$GPTB_EXECUTABLE" shell-event finished \
        "$GPTB_ACTIVE_INTERACTION_ID" \
        "$exit_code"

    GPTB_ACTIVE_INTERACTION_ID=""

    # Do not let gptbridge's hook interfere with other zshexit hooks
    return 0
}

# Remove any previous gptbridge registrations first. This makes the integration
# safe to source more than once without accumulating duplicate hook callbacks.
add-zsh-hook -d preexec _gptb_preexec 2>/dev/null
add-zsh-hook -d precmd _gptb_precmd 2>/dev/null
add-zsh-hook -d zshexit _gptb_zshexit 2>/dev/null

# Register the command-start and shell-exit hooks normally
add-zsh-hook preexec _gptb_preexec
add-zsh-hook zshexit _gptb_zshexit

# Register precmd, then move it to the from of precmd_functions. Command
# capture must end before prompt-related hooks emit terminal formatting or
# metadata that does not belong to the completed command's output.
add-zsh-hook precmd _gptb_precmd
precmd_functions=(
    _gptb_precmd
    ${precmd_functions:#_gptb_precmd}
)
