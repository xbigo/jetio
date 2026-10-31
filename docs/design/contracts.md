# Design by contract

Every public function states its contract and the library checks it where that is feasible. C++23 has no contracts, so they are simulated by macros that map one-to-one to the C++26 syntax.

| Macro | C++26 | Meaning |
| --- | --- | --- |
| `JIO_Expects(cond)` | `pre(cond)` | Precondition: the caller's obligation, checked on entry |
| `JIO_Ensures(cond)` | `post(cond)` | Postcondition: the callee's obligation, checked before returning |
| `JIO_Assert(cond)` | `contract_assert(cond)` | Internal assertion |
| `JIO_Expects_critical`, `JIO_Assert_critical` | `pre`, `contract_assert` | Same, but also checked in release builds |
| `JIO_Expects_audit`, `JIO_Ensures_audit`, `JIO_Assert_audit` | none | Same, for checks too expensive for every build; C++26 has no audit level |

Header: `jetio/std/contract.hpp`: the contract facility is a language extension, and language extensions live in the `std` subdirectory (the namespace is `jetio`, because `jetio::std` would hide the real `std`). These are the only macros in the public interface of the library, apart from the `JIO_CONTRACT_LEVEL` switch. They are not exported by a C++ module; the checks stay compiled into the module. A `JIO_Ensures_critical` does not exist yet; add it when a postcondition needs it.

## What is a contract violation and what is an error

- A **contract violation** is a programming error that the caller could have avoided by looking at the public interface: an invalid range, a null pattern, calling `seek_forward` backwards.
- An **error** is a failure the caller cannot check beforehand: IO failure, out of memory, a size that does not fit the platform. Errors are reported through `error_code_ptr` or `io_error`, see [io protocol](io-protocol.md).
- If the caller can check a condition with the public interface and the device behaves deterministically, it is a precondition. Conditions that depend on state that can change behind the device's back (file size) are errors.

Pointer parameters may always be null, references never are. Only the exceptions to that are documented.

## Documentation format

Each function documents, in this order, with Doxygen tags:

- `@pre` preconditions; (audit) marks audit-level ones
- `@return` what the result means
- `@post` postconditions
- errors reported (`errc::...`) and the exception safety guarantee, when not the default of the class

Postconditions that need the result are checked by assigning the result to a local first, then `JIO_Ensures`, then `return`.

## Checking levels

`JIO_CONTRACT_LEVEL` (CMake: `JETIO_CONTRACT_LEVEL`):

| Level | Checked | Default for |
| --- | --- | --- |
| 0 | Nothing; conditions are compiled but not evaluated | |
| 1 | Critical | `NDEBUG` (release) |
| 2 | Critical and default | |
| 3 | Critical, default and audit | No `NDEBUG` (debug) |

**Contracts are free in release unless marked critical.** Checking a contract is the right of the callee, not a duty: a program that violates one is already broken, and the callee is entitled to rely on it without checking. Beyond that, performance matters. Contracts are the core of the design, so writing one must never need a performance discussion. The price is that a violation in a release build is undefined behavior unless the contract is critical, so the criterion for critical is about consequences, not cost.

A check is **critical** if all of these hold:

- A violation would otherwise cause memory unsafety, undefined behavior, or data corruption that cannot be detected later. Typical: it guards pointer arithmetic or a narrowing conversion that feeds into it, or a division.
- The check is O(1) and does not call into a device.

Everything else is a plain contract. A check that is expensive or calls into a device is **audit**. When in doubt, use the plain macro and say in the review why it is not critical.

The critical checks today: validity of the range given to `memory_device::view_rd` and `view_wr`, the narrowing conversions `to_size_t`, `offset_to_size_t` and `to_ptrdiff_t`, and the pattern of `fill_device` not being empty.

Level 3 in an optimized build is available as the `*-release-audit` presets. The benchmarks in `tests/bench` show what checking costs; run them in a release preset and its `-audit` twin.

Use the same level in the whole program. Different levels in different translation units are an ODR violation for the inline functions. Tests of violations need a level that checks the contract, they skip themselves otherwise, so they only run in the debug and sanitizer presets (and the `-audit` ones), not in plain release.

## Violation handling

A violated contract means the program is in an undefined state, so there is no way to go on, and no handler may return. `handle_contract_violation` calls the handler set by `set_contract_violation_handler`; if the handler returns, the program aborts. The default handler prints the location and the expression to stderr and aborts.

A handler may throw. That exists for white-box tests, of the facility itself and of functions with preconditions, where the test must get past the violation: their handler throws and the test catches the exception. Production code should terminate.

## noexcept

A function with a precondition is not `noexcept`, because a violation may throw from a test handler, and a function that is `noexcept` has no precondition. Functions with a wide contract (`offset`, `size`, `is_eof`, most pseudo device operations) are `noexcept` where nothing can throw.

## Exception safety

All code is exception safe, mainly through RAII: no raw owning pointers, no cleanup code that is not in a destructor. `try`/`catch` appears only where an exception must become an error code. Today that is one place, `memory_device::resize_data`, which turns `std::bad_alloc` and `std::length_error` into `errc::not_enough_memory` and `errc::value_too_large`.

Guarantees used in the docs: nothrow, strong (no change on failure), basic (valid state, no leaks). Destructors never throw, so a device that must write on destruction offers an explicit operation instead and the destructor does not report errors.
