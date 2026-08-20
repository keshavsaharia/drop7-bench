# Denoised Public-State Value Model

`v1.bin` is a 141,780-byte binary model with magic identifier `D7DNV001` and
SHA-256:

```text
c8090759f3719fea8eb350dc1adf59e8578e6a36c1e380001a74e2c9470ef2fc
```

It is consumed by:

- `approaches/value-policy-learning/denoised-value/denoised-guided-veto.cpp`;
  and
- `approaches/value-policy-learning/denoised-value/d4-phase5-value-veto.cpp`.

The model was retained because these experiments load it directly. Recorded
training and evaluation details are in the
[experiment ledger](../../../docs/research/history.md#denoised-stochastic-public-state-value).
