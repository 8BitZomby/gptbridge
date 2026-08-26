cat > README.md <<'EOF'
# gptbridge

`gptbridge` is a C++ terminal integration tool that captures shell interactions
and makes selected terminal context and project files available to
MCP-compatible clients.

The executable is named `gptb`.

> **Current version:** 0.2.1  
> **Status:** Active development

## Features

- Captures commands and terminal output from a managed zsh shell.
- Stores captured interactions in persistent logical sessions.
- Associates logical sessions with registered projects.
- Separates complete terminal history from intentionally pushed terminal context.
- Supports persistent `append` and `replace` push modes.
- Exposes terminal context and project files through an MCP server.
- Supports project-local `.gptignore` rules.
- Restricts MCP filesystem access using path-containment, symlink,
  sensitive-path, visibility, and file-size checks.
- Preserves logical sessions independently from individual terminal attachments.

## Requirements

gptbridge currently targets macOS and requires:

- a C++20 compiler;
- CMake 3.20 or newer;
- zsh.

`nlohmann/json` is retrieved automatically through CMake `FetchContent`.

## Setup

### 1. Build gptbridge

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

The resulting executable is:

```text
build/gptb
```

Check the built version with:

```bash
build/gptb --version
```

The zsh integration is embedded into the executable at build time. The
`shell/gptbridge.zsh` source file does not need to be installed or sourced
directly.

### 2. Make `gptb` available on `PATH`

For development, add the build directory to `PATH`:

```bash
export PATH="/path/to/gptbridge/build:$PATH"
```

To make this permanent, add the corresponding line to your shell
configuration.

Verify that the executable is available:

```bash
command -v gptb
gptb --version
```

### 3. Enable the zsh integration

Add this line to `~/.zshrc`:

```zsh
eval "$(gptb shell-init zsh)"
```

Then open a new terminal or reload the configuration:

```bash
source ~/.zshrc
```

The generated integration activates only inside a shell launched and managed
by gptbridge. In an ordinary zsh session, the integration remains inactive.

When gptbridge launches a managed shell, it supplies the environment needed by
the embedded zsh integration. The installed hooks then report command metadata
and lifecycle boundaries through the managed PTY.

### 4. Create a project and logical session

Create a project and start a managed shell with:

```bash
gptb init <path> <project-name>
```

For example:

```bash
gptb init ~/Documents/my-project my-project
```

This registers the project, creates a logical session, and launches the
PTY-backed managed shell associated with that session.

Inside the managed shell, check the current state with:

```bash
gptb status
```

An existing logical session can later be restored with:

```bash
gptb restore
```

or:

```bash
gptb restore <session-id>
```

## MCP setup

gptbridge includes its MCP server in the same `gptb` executable.

The internal MCP server entry point is:

```text
gptb mcp-server
```

This command is intended to be launched by an MCP client rather than used
interactively.

### 1. Find the `gptb` executable

Run:

```bash
command -v gptb
```

Use the resulting executable path when configuring the MCP client.

Using an absolute path is useful for graphical applications because they may
not inherit the same `PATH` as an interactive terminal.

### 2. Configure the MCP client

The exact configuration location depends on the MCP client. A typical server
definition is:

```json
{
  "mcpServers": {
    "gptbridge": {
      "command": "/absolute/path/to/gptb",
      "args": ["mcp-server"]
    }
  }
}
```

Replace `/absolute/path/to/gptb` with the path reported by:

```bash
command -v gptb
```

Restart the MCP client after initially adding or changing the server
configuration so that it launches the gptbridge MCP server.

### 3. Select the logical session exposed through MCP

From inside the managed gptbridge shell whose state should be exposed, run:

```bash
gptb mcp sync
```

A successful synchronization reports the selected session and project:

```text
MCP session: <session-id>
MCP project: <project-name>
```

The selected logical session is stored in gptbridge's MCP state. Switching
sessions therefore does not require creating a new MCP server configuration.

### 4. Push terminal context

Captured terminal history and persistent terminal context are separate.

To push recent interactions into persistent context:

```bash
gptb push <count>
```

For example:

```bash
gptb push 5
```

The persistent push mode can be changed with:

```bash
gptb push append
```

or:

```bash
gptb push replace
```

`append` keeps the existing persistent context and adds the selected
interactions.

`replace` discards the existing persistent context and replaces it with the
selected interactions.

Project files are exposed independently through the MCP project-file tools.

## How it works

gptbridge runs an interactive zsh shell behind a pseudo-terminal (PTY).

The user's terminal still behaves like a normal interactive terminal, but
gptbridge sits between the real terminal and the managed shell. This allows it
to observe three distinct streams of information:

1. user input sent to the shell;
2. ordinary PTY output produced by the shell and commands;
3. shell-integration control metadata used to define command boundaries.

A typical captured interaction works as follows:

1. The user enters or pastes a shell submission.
2. gptbridge forwards the input to the PTY-backed zsh process.
3. zsh's gptbridge integration reports the current working directory and exact
   accepted command.
4. `OSC 133;C` marks the authoritative start of command execution.
5. Ordinary PTY bytes are captured as the command's output.
6. `OSC 133;D;<exit-code>` marks the authoritative completion boundary.
7. gptbridge finalizes and stores the resulting `TerminalInteraction`.
8. Selected interactions can later be copied into persistent context with
   `gptb push`.
9. The MCP server can expose that pushed context and permitted project files to
   an MCP-compatible client.

One accepted shell submission corresponds to one `TerminalInteraction`.

A submission may itself contain multiple simple commands, pipelines, compound
commands, or multiple lines. gptbridge records the accepted shell submission
as one interaction rather than attempting to split it into individual shell
language constructs.

## Architecture

### Data path 1: user input

```text
keyboard / real terminal
          |
          v
        gptb
          |
          | forwardTerminalInput()
          v
         PTY
          |
          v
         zsh
```

`forwardTerminalInput()` carries bytes entered through the real terminal into
the PTY connected to the managed zsh process.

This path handles terminal input only. Command lifecycle information is carried
separately through shell-integration control sequences.

### Data path 2: PTY output

```text
         zsh
          |
          v
         PTY
          |
          | forwardPtyOutput()
          v
        gptb
          |
          v
 real terminal
```

`forwardPtyOutput()` receives bytes produced through the managed PTY.

Ordinary output is forwarded to the real terminal so the shell remains
interactive. During an active command lifecycle, the appropriate ordinary PTY
bytes are also recorded as output belonging to the current
`TerminalInteraction`.

gptbridge also recognizes shell-integration control sequences in this stream
so its private metadata can be handled separately from terminal presentation.

### Data path 3: shell control metadata

```text
zsh hooks / shell integration
             |
             | OSC sequences
             v
           gptb
             |
             v
    CaptureCoordinator
```

The control path carries semantic information about the shell rather than
ordinary command output.

The current protocol uses:

- **OSC 7** for the current working directory;
- a **private GPTB OSC** for the exact accepted command text and
  gptbridge-specific metadata;
- **OSC 133;C** as the authoritative command-start boundary;
- **OSC 133;D;<exit-code>** as the authoritative command-completion boundary.

The private GPTB metadata includes an attachment-specific nonce so gptbridge
can distinguish its own shell integration from unrelated terminal integration
sequences.

### Command lifecycle

The command lifecycle is:

```text
shell accepts submission
          |
          v
        OSC 7
 current working directory
          |
          v
   private GPTB OSC
 exact command / metadata
          |
          v
      OSC 133;C
 authoritative start
          |
          v
 capture ordinary PTY output
          |
          v
 OSC 133;D;<exit-code>
 authoritative completion
          |
          v
 store TerminalInteraction
```

Command metadata can be known before execution begins, but metadata alone does
not start output capture.

`OSC 133;C` is the authoritative boundary that begins command output capture.

`OSC 133;D;<exit-code>` is the authoritative completion boundary that
finalizes the interaction with its exit status.

### Presentation boundary

zsh can emit shell-generated presentation bytes around command execution, such
as partial-line markers.

gptbridge uses a private shell-presentation marker to distinguish those bytes
from command output where necessary. The zsh integration temporarily wraps
`PROMPT_EOL_MARK` for the active command and restores the user's original
configuration afterward.

This prevents shell presentation from being incorrectly persisted as command
output while preserving the visible interactive terminal behavior.

## Logical sessions

A gptbridge logical session is separate from the terminal device currently
attached to it.

Conceptually:

```text
terminal attachment
        |
        v
 logical session
        |
        +-- stable session ID
        +-- active project
        +-- captured terminal history
        +-- persistent terminal context
        +-- runtime attachment state
```

A logical session therefore survives independently from a specific TTY.

Closing a live session terminates its managed-shell attachment while preserving
the saved session data.

A preserved session can later be restored and attached to another terminal.

Sessions can be inspected with:

```bash
gptb session list
```

## Terminal history and persistent context

gptbridge intentionally separates complete captured terminal history from the
smaller context selected for MCP use.

```text
captured terminal interactions
             |
             | gptb push
             v
  persistent terminal context
             |
             v
         MCP server
```

This allows the session to retain its full local interaction history while
exposing only explicitly selected interactions as persistent context.

### Append mode

```text
existing persistent context
            +
 selected interactions
            |
            v
 new persistent context
```

### Replace mode

```text
 selected interactions
            |
            v
 new persistent context
```

The configured mode is persistent and can be changed with:

```bash
gptb push append
gptb push replace
```

## MCP integration

The MCP server reads the logical session selected in gptbridge's MCP state.

Through MCP, a client can access:

- persistent terminal context;
- the active project's visible files;
- project-file listings;
- project-file search;
- individual permitted project files.

Project filesystem access is read-only.

### Filesystem protections

MCP project-file operations enforce several layers of protection:

- requested files must remain inside the registered project;
- symlink targets must also resolve inside the registered project;
- sensitive paths are excluded;
- shared project-visibility rules are enforced;
- project-local `.gptignore` rules are enforced;
- direct file reads are limited to 1 MiB;
- project search skips files larger than 1 MiB and reports when files were
  skipped;
- expected filesystem failures are handled through non-throwing filesystem
  operations where appropriate.

Internal file symlinks may be exposed under their project-local alias when both
the alias and resolved target satisfy the visibility rules.

Symlinks that resolve outside the project are rejected.

## `.gptignore`

A registered project can contain a project-local `.gptignore` file to exclude
paths from MCP access.

The file is optional and is not created automatically.

Ignored files are excluded from:

- project-file listings;
- project-file search;
- direct MCP file reads.

Ignored directories are pruned during recursive traversal.

### Supported patterns

The current `.gptignore` implementation supports:

- blank lines;
- `#` comments;
- exact file and path patterns;
- trailing `/` for directories and their descendants;
- `*` for zero or more non-`/` characters;
- `?` for exactly one non-`/` character;
- `**` across directory boundaries;
- leading `/` for project-root anchoring.

Example:

```gitignore
# Build output
generated/
*.o

# Temporary files below any directory
**/tmp/

# Root-only path
/config/local.json
```

Negated `!` patterns are not currently supported.

## Command-line interface

General usage:

```text
gptb <command> [arguments]
```

### Sessions

| Command | Description |
| --- | --- |
| `gptb init <path> <project-name>` | Register a project and create a new logical session. |
| `gptb restore [session-id]` | Restore the most recent saved session or a specified session. |
| `gptb status` | Show the session and project attached to the current shell. |
| `gptb session list` | List saved logical sessions and their runtime state. |
| `gptb session close <session-id>` | Close a live session while preserving its saved data. |
| `gptb session delete <session-id>` | Permanently delete an inactive logical session. |
| `gptb session restore [session-id]` | Restore the most recent saved session or a specified session. |

### Projects

| Command | Description |
| --- | --- |
| `gptb project list` | List registered projects. |
| `gptb project add <name> <path>` | Register a project without changing the current session. |
| `gptb project remove <name>` | Unregister a project without deleting its files. |
| `gptb use <project-name\|.>` | Change the active project for the current session. |

Using `.` with `gptb use` selects the registered project associated with the
current working directory.

### Terminal context

| Command | Description |
| --- | --- |
| `gptb push` | Push terminal interactions using the saved push mode. |
| `gptb push <count>` | Push the selected number of recent terminal interactions. |
| `gptb push append` | Set the persistent push mode to append. |
| `gptb push replace` | Set the persistent push mode to replace. |
| `gptb show` | Display persistent terminal context. |
| `gptb clear` | Clear persistent terminal context. |

### MCP

| Command | Description |
| --- | --- |
| `gptb mcp sync` | Synchronize MCP with the logical session attached to the current shell. |

`gptb mcp-server`, `gptb shell-init`, and `gptb shell-event` are internal
integration entry points rather than normal user-facing workflow commands.

### General options

| Option | Description |
| --- | --- |
| `-h`, `--help` | Display command-line help. |
| `-V`, `--version` | Display the current gptbridge version. |
EOF
