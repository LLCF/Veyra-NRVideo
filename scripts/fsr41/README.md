# Local FSR 4.1.1 INT8 provider

Source: https://github.com/int3rrobang/fsr4-int8-reverse-engineering

Pinned revision: `88635b94083965a7c3b5f64e099808b8ba2ce576` (MIT; LICENSE.txt).
The source checkout and all generated data live outside Veyra Git.

`provider-veyra.patch` adapts `bench/provider411/provider.h`, `provider.cpp`,
`frame.cpp`, and `ffx_api_impl.cpp`:

- Six descriptor/CBV slots, selected by Veyra's fence-retired command slot via
  `veyraFsr411SetSlot(context, slot, 1)` before every dispatch.
- Reset clears scratch/history/recurrent/reprojection on the caller list,
  with immutable clear descriptors. No reset-time private queue/CPU wait.
- Owned resources are released on context destruction.
- Approximate auto-exposure is refused. Video uses explicit exposure 1.

Build with `build-provider.ps1`. Supply the external checkout, external build
and temporary directories, `SdkRoot` pointing to `Kits/FidelityFX`, the local
official 4.1.1 DLL, Python and Windows SDK DXC. `ReuseGenerated` skips codegen
only when rebuilding an existing generation. VS 2022 is required by this script.

Source data used locally: FidelityFX SDK 2.3.0 revision
`60f4ea81909200d8542eca14dccb2628b763a9a3`, official upscaler 4.1.1.2740,
SHA256 `D0DCCCC74A43C44BA435B7A369B456E0970D8A4464E4BD683119B374F2C9FB46`.
The SDK `docs/license.md` explicitly lists the upscaler DLL in its MIT exception;
upstream `NOTICE.md` provides additional provenance. Neither document makes
NVIDIA/Intel components MIT. Official data, weights, containers, generated
shader arrays and the output DLL must never be committed to this repository.

Opt in only for the current process using `VEYRA_FSR41_PROVIDER=<absolute DLL>`
and select existing FSR SR mode 5. Without the variable, the existing official
FSR path is used. A missing/incompatible experimental provider is reported;
the engine disables SR rather than reporting an implicit FSR3 success.

This is a research integration, not AMD support for NVIDIA. It uses zero
jitter, estimated motion and constant depth. The upstream model's middle
working class is fixed at 960x540 even though PRE/POST accept other geometries.
Do not infer native 4K model quality or game-equivalent reconstruction from
successful dispatch. The interface remains diagnostic until video quality,
display behavior and the RTX 30/40 hardware matrix have been reviewed.
