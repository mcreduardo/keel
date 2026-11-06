# keel

Structure for C++ applications: components with declared dependencies, a composition
checked by the compiler, and startup and shutdown order computed for you.

Single header, C++20, no dependencies besides the standard library. Work in progress.

## Building the tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## License

[MIT](LICENSE)

## AI Usage Disclaimer

Portions of this repository - including code, comments, and documentation - were generated or assisted by artificial intelligence tools.

All AI-generated outputs have been manually reviewed, tested, and refined by the project maintainers to ensure accuracy, security, and compliance with the repository's licensing terms.
