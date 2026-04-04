# 🧊 esp-ice

*Ice ice baby. Too cold -- slice like a ninja, cut like a razor blade.* 😉

## Contributing

Contributions to **esp-ice** are welcome! Please follow these guidelines:

### Development Setup

Before contributing, set up your development environment:

1. Clone the repository:
   ```bash
   git clone https://github.com/espressif/esp-ice.git
   cd esp-ice
   ```

2. Install pre-commit hooks:
   ```bash
   pip install pre-commit
   pre-commit install -t pre-commit -t commit-msg
   ```

   The pre-commit hooks will automatically run before each commit to check code formatting and commit message standards.

### Code Standards

- Follow the existing C code style (enforced by `clang-format`)
- Ensure all commits follow the [Conventional Commits](https://www.conventionalcommits.org/) standard
- Run the test suite before submitting pull requests

### Submitting Changes

1. Create a new branch for your changes
2. Make your changes and ensure pre-commit checks pass
3. Submit a pull request with a clear description of the changes

For more details, see the pre-commit configuration in `.pre-commit-config.yaml`.

## License

This project is licensed under the Apache License 2.0. See the [LICENSE](LICENSE) file for details.

Copyright 2026 Espressif Systems (Shanghai) CO LTD
