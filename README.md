# vmd (Local Version Control Tool)

**vmd** is a lightweight, fast, and simple command-line version control system written in pure C. It is designed to help you manage and track project versions locally on your machine without the overhead of heavy version control systems.

---

## Features

- 🚀 **Lightning Fast:** Written entirely in C with direct POSIX file system operations.
- 📂 **Local Storage:** Automatically organizes and stores project snapshots in `~/vmd/projects`.
- 🔢 **Automatic Versioning:** Easily increment versions (`v1`, `v2`, `v3`...) with a single command.
- 📦 **Export Capabilities:** Export any specific version of a project to any target directory.
- 🗑️ **Flexible Cleanup:** Delete specific versions, entire projects, or clear all data easily.
- 📋 **Listing Support:** View all tracked projects or inspect available versions for a specific project.

---

## Commands Reference

| Command | Description |
| :--- | :--- |
| `vmd add <project_name>` | Run inside your project folder. Saves the current state as version 1 (`v1`). |
| `vmd up <project_name>` | Run inside your project folder. Saves the current state as the next version (`v2`, `v3`, ...). |
| `vmd imp <name> <version> <path>` | Exports a specific version to a target folder named `vmd-<project>_v-<version>`. |
| `vmd list` | Lists all tracked projects. |
| `vmd list <project_name>` | Lists all saved versions of a specific project. |
| `vmd rm <project_name>` | Removes the project and all its versions. |
| `vmd rm <project_name> <version>` | Removes only a specific version of a project. |
| `vmd rm <project_name> all` | Removes all versions of a specific project. |
| `vmd rm all` | Completely clears and removes every saved project. |
| `vmd help` | Displays the help menu and usage guide. |

---

## Installation & Setup

If you have an installation script (`install.sh`), you can set up the tool automatically:

1. Clone or download the repository.
2. Run the installation script in your terminal:
   ```bash
   chmod +x install.sh
   ./install.sh
