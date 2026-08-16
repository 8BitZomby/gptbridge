# gptbridge zsh integration
#
# Loaded only inside a shell launched by `gptb capture`.
# Installs shell lifecycle hooks that report command metadata and semantic
# boundaries through terminal control sequences in the captured PTY stream.
#
# GPTB_SESSION_NONCE is supplied by the parent before the shell is executed.
# Private GPTB metadata includes this nonce so the parent can verify that the
# metadata belongs to the current capture session.


# Do nothing in ordinary shells. PtyCaptureBackend sets this variable only for
# shells launched inside a gptbridge capture session.
[[ -n "${GPTB_SESSION_NONCE:-}" ]] || return

# The captured shell must know the exact gptb executable that launched it so
# lifecycle hooks do not depend on PATH resolution.
[[ -n "${GPTB_EXECUTABLE:-}" ]] || return


# Load zsh's hook-registration helper so gptbridge can install lifecycle hooks
# without replacing hooks registered by other shell integrations.
autoload -Uz add-zsh-hook


# Tracks whether gptbridge has emitted OSC 133;C for a command that has not yet
# received its matching OSC 133;D completion marker.
typeset -gi GPTB_COMMAND_ACTIVE=0

# Records whether PROMPT_EOL_MARK was explicitly set before gptbridge wrapped
# it for the current command.
typeset -gi GPTB_PROMPT_EOL_MARK_WAS_SET=0

# Records whether gptbridge currently owns a temporary PROMPT_EOL_MARK wrapper.
typeset -gi GPTB_PROMPT_EOL_MARK_WRAPPED=0

# Preserves the exact explicit PROMPT_EOL_MARK value that existed before the
# current command began.
typeset -g GPTB_SAVED_PROMPT_EOL_MARK=""

# Stores the exact temporary value assigned by gptbridge. Comparing against this
# value lets restoration avoid overwriting a change made by the user's command.
typeset -g GPTB_WRAPPED_PROMPT_EOL_MARK=""


# Restores PROMPT_EOL_MARK after zsh has had an opportunity to emit its
# partial-line presentation for the completed command.
_gptb_restore_prompt_eol_mark() {
    if (( ! GPTB_PROMPT_EOL_MARK_WRAPPED )); then
        return 0
    fi

    # Restore only when PROMPT_EOL_MARK still contains the wrapper installed by
    # gptbridge. If the command changed the parameter itself, preserve that new
    # value instead of overwriting the user's change.
    if [[ "${PROMPT_EOL_MARK-}" == "$GPTB_WRAPPED_PROMPT_EOL_MARK" ]]; then
        if (( GPTB_PROMPT_EOL_MARK_WAS_SET )); then
            # The parameter was explicitly set before the command, so restore
            # its exact previous value.
            PROMPT_EOL_MARK="$GPTB_SAVED_PROMPT_EOL_MARK"
        else
            # The parameter was originally unset, so return zsh to its normal
            # built-in PROMPT_EOL_MARK default behavior.
            unset PROMPT_EOL_MARK
        fi
    fi

    # The saved values apply only to one command lifecycle.
    GPTB_PROMPT_EOL_MARK_WAS_SET=0
    GPTB_PROMPT_EOL_MARK_WRAPPED=0
    GPTB_SAVED_PROMPT_EOL_MARK=""
    GPTB_WRAPPED_PROMPT_EOL_MARK=""

    return 0
}


# Called by zsh immediately before an interactive command begins executing.
# $1 contains the complete command text supplied to the preexec hook.
_gptb_preexec() {
    local presentation_marker

    # Generate the private GPTB marker that identifies where zsh's partial-line
    # presentation begins. Command substitution captures the encoded bytes so
    # they can be embedded into PROMPT_EOL_MARK rather than emitted immediately.
    presentation_marker="$(
        "$GPTB_EXECUTABLE" shell-event osc-presentation-start
    )" || return 0

    # Remember whether PROMPT_EOL_MARK was explicitly set so restoration can
    # distinguish an explicit value from zsh's implicit default.
    GPTB_PROMPT_EOL_MARK_WAS_SET=${+PROMPT_EOL_MARK}

    if (( GPTB_PROMPT_EOL_MARK_WAS_SET )); then
        GPTB_SAVED_PROMPT_EOL_MARK="$PROMPT_EOL_MARK"
    else
        GPTB_SAVED_PROMPT_EOL_MARK=""
    fi

    local effective_prompt_eol_mark

    if (( GPTB_PROMPT_EOL_MARK_WAS_SET )); then
        # Preserve the user's explicitly configured partial-line marker.
        effective_prompt_eol_mark="$GPTB_SAVED_PROMPT_EOL_MARK"
    else
        # When PROMPT_EOL_MARK is unset, zsh behaves as though this default
        # prompt value had been configured.
        effective_prompt_eol_mark='%B%S%#%s%b'
    fi

    # Prefix zsh's normal partial-line presentation with the private GPTB marker.
    # %{...%} tells zsh's prompt engine that the OSC bytes occupy no terminal
    # columns while still emitting those bytes into the PTY stream.
    GPTB_WRAPPED_PROMPT_EOL_MARK="%{${presentation_marker}%}${effective_prompt_eol_mark}"
    PROMPT_EOL_MARK="$GPTB_WRAPPED_PROMPT_EOL_MARK"
    GPTB_PROMPT_EOL_MARK_WRAPPED=1

    # Emit command metadata in semantic order:
    #
    #   OSC 7       current working directory
    #   GPTB;E      exact command text and session nonce
    #   OSC 133;C   authoritative command-output start
    #
    # shell-event writes these sequences into the captured PTY stream through
    # its standard output.
    if "$GPTB_EXECUTABLE" shell-event osc-started \
        "$1" \
        "$PWD"
    then
        # A successful OSC 133;C emission opens the command lifecycle that the
        # next precmd or zshexit hook must complete with OSC 133;D.
        GPTB_COMMAND_ACTIVE=1
    else
        # Without an active gptbridge lifecycle there is no reason to retain the
        # temporary PROMPT_EOL_MARK wrapper.
        _gptb_restore_prompt_eol_mark
    fi

    # Shell-integration failures must not prevent the user's command or later
    # preexec hooks from running.
    return 0
}


# Called by zsh before displaying the next prompt after a command completes.
_gptb_precmd() {
    # Save the user's command exit status immediately. Any command executed
    # below would otherwise replace the value stored in $?.
    local exit_code=$?

    # The initial prompt also invokes precmd before any command has executed.
    # Without a matching OSC 133;C lifecycle there is nothing to complete.
    if (( ! GPTB_COMMAND_ACTIVE )); then
        return 0
    fi

    # zsh's partial-line handling occurs before this hook. Restore the user's
    # PROMPT_EOL_MARK now that its temporary presentation marker is no longer
    # needed for the completed command.
    _gptb_restore_prompt_eol_mark

    # Emit OSC 133;D with the exit status produced by the completed command.
    "$GPTB_EXECUTABLE" shell-event osc-finished \
        "$exit_code"

    # The command has finished regardless of whether internal reporting
    # succeeded, so a later prompt or shell exit must not complete it again.
    GPTB_COMMAND_ACTIVE=0

    # Do not prevent other precmd hooks from running.
    return 0
}


# Called when zsh itself is about to exit. This completes a command such as
# `exit`, whose preexec hook runs but which never reaches another precmd hook.
_gptb_zshexit() {
    # Capture the shell's exit status before any hook logic can overwrite $?.
    local exit_code=$?

    if (( ! GPTB_COMMAND_ACTIVE )); then
        return 0
    fi

    # No next prompt will be displayed, so restore any temporary prompt state
    # before the shell terminates.
    _gptb_restore_prompt_eol_mark

    # Emit the final OSC 133;D marker for the command that caused the shell
    # itself to terminate.
    "$GPTB_EXECUTABLE" shell-event osc-finished \
        "$exit_code"

    GPTB_COMMAND_ACTIVE=0

    # Do not let gptbridge's hook interfere with other zshexit hooks.
    return 0
}


# Remove previous gptbridge registrations first so sourcing this integration
# again cannot accumulate duplicate lifecycle callbacks.
add-zsh-hook -d preexec _gptb_preexec 2>/dev/null
add-zsh-hook -d precmd _gptb_precmd 2>/dev/null
add-zsh-hook -d zshexit _gptb_zshexit 2>/dev/null


# preexec reports metadata immediately before command execution, while zshexit
# closes commands that terminate the interactive shell itself.
add-zsh-hook preexec _gptb_preexec
add-zsh-hook zshexit _gptb_zshexit


# Register gptbridge's precmd hook before other prompt-related precmd hooks.
# Completing the active interaction first prevents output subsequently produced
# by prompt hooks from being associated with the command that just finished.
add-zsh-hook precmd _gptb_precmd

precmd_functions=(
    _gptb_precmd
    ${precmd_functions:#_gptb_precmd}
)
