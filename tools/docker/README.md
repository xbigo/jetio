# Container security profile

`seccomp.json` is the default seccomp profile of Docker (`moby/profiles`, `seccomp/default.json`, main branch, checked against Docker 28.5.2) plus one rule at the end: `personality` is allowed with the argument `0x40000` (`ADDR_NO_RANDOMIZE`). TSan needs it to switch off address space randomization for itself when the kernel randomizes more than its memory layout allows; without it the TSan tests fail at start with "unable to disable ASLR". The rule only affects the address layout of the calling process. `tools/jetio-dev` passes the profile to every container.

When Docker is upgraded, compare with the current default profile and keep the one added rule.
