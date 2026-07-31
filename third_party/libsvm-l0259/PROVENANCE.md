# LIBSVM provenance for L0259

- Upstream: `https://github.com/cjlin1/libsvm`
- Frozen revision: `6b907139084abf2da4d6d3cb10dc3b7eaffa2fbb`
- License: BSD 3-Clause; preserved verbatim in `COPYRIGHT`
- `svm.cpp` SHA-256: `bfe02a7c001151ab703c206ff6a9136c75eb704b4427536e3f65d3ac90bd0eb6`
- `svm.h` SHA-256: `597aff50cfb9a04922736472bd125862439c87408a83cb57fb04980744dffd51`

L0259's author source calls `sklearn.svm.SVR`, whose native solver is
LIBSVM. The platform vendors the official C++ implementation so the
paper's RBF epsilon-SVR training remains a pure-C++ reproducible backend.
No scientific behavior in the vendored files was modified.
