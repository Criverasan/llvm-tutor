
cat > README.md << 'EOF'
# LLVM Loop Optimization Passes
### CS 4390 – Advanced Compilers  
### Cristina Rivera-Sanchez

This repository extends the [llvm-tutor](https://github.com/banach-space/llvm-tutor) infrastructure with two loop optimization passes:

- **SimpleLICM** — a simplified Loop-Invariant Code Motion (LICM) pass  
- **DerivedIVElim** — a basic Derived Induction Variable Elimination (IVE) pass  

Both passes are implemented and tested within the LLVM New Pass Manager framework.

---

## Build Instructions
Make sure you have LLVM installed (e.g., via Homebrew on macOS):

```bash
brew install llvm

mkdir -p build && cd build
cmake -G Ninja \
  -DLT_LLVM_INSTALL_DIR="$(brew --prefix llvm)" \
  -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm" ..
ninja SimpleLICM DerivedIVElim

cd ~/llvm-tutor
