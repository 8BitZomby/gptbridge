# gptbridge zsh integration
#
# Loaded only inside a shell launched by `gptb capture`.
# Installs shell lifecycle hooks that report command-start and command-finish
# metadata to the parent process through gptbridge's private control protocol.
#
# GPTB_SESSION_NONCE is supplied by the parent before the shell is executed,
# allowing emitted control frames to be authenticated as belonging to the
# current capture session.
