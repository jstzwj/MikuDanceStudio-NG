# Complete-scene visual acceptance

On 2026-11-04 the user changed complete-frame RGB acceptance from strict pixel
equality/tolerance to visual equivalence after screenshot comparison. The strict
RGB-one-code-value, exact-Alpha and zero-coverage-difference profile remains
available with `MDS_COMPOSITE_STRICT=1`, and every run prints its unchanged counts.
Historical strict failures remain failures under that profile. Original PNGs and
production rendering are unchanged.

Fresh local runs pass both frames on DX11/DX12 hardware, DX11/DX12 WARP and Vulkan
hardware. The new WARP captures were also reviewed side by side at their original
size: [rest](evidence/p4-visual-rest.png) and [posed](evidence/p4-visual-posed.png).
Repeated submission remains byte-exact in every profile. The generated full-size
overviews contain the unscaled DX9 reference on the left and actual output on the
right; only their diagnostic labels are added.

The existing DX9 reference and WARP, Metal and software-Vulkan rest/posed captures
were reviewed as complete images and nearest-neighbor difference crops. Their
observed differences occupy isolated grid-line and projected-shadow edge pixels;
there was no visible object, material, orientation or scene-layout discrepancy at
the original display size. In the DX11 diagnostic, the first rest difference is
line coverage at draw 2; the posed difference starts with projected-shadow
depth/stencil at draw 5. Default VS positions were already observed to match bit
for bit. Raw D24S8 prefixes now show that the coplanar y=0.1 grid/shadow can differ
by one or two depth units and change stencil acceptance. This explains a subset
of the observed image differences; it is not a general backend equivalence claim.

`tests/composite_visual_compare.hpp` makes this review repeatable for complete
editor composites. It compares both black and white background compositions, so
Alpha is exercised instead of being dropped. Both backgrounds must satisfy:

| Measurement | Limit |
| --- | --- |
| Mean absolute RGB error, in 8-bit code values | 0.5 |
| Fraction of pixels with any RGB error greater than 3 | 0.0005 (0.05%) |
| Maximum absolute mean signed RGB error in an 8×8 block | 12 |

Partial edge blocks use their real pixel count. There is no image alignment,
blur, resampling, excluded mask or reference replacement in this comparison.
Alpha difference counts are reported separately, and their visible contribution
is checked on both backgrounds. Positive numerical results support the reviewed
visual profile; unfamiliar failure patterns still require screenshot inspection.
Counterexamples reject full-frame tint, lost Alpha and even an 8×8 missing object.

This policy applies **only** to the complete editor scene composite gate. Texture
and postprocess copies, repeated submissions, floating shadow/depth tests and
resource-lifecycle contracts retain their current exact/numerical requirements.
It does not relax UI geometry or input behavior. The changed policy does not close
Windows native IME or the remaining shown control/font/DPI qualification gaps.

`tools/analyze_editor_composite.py` records both visual and strict results and
retains every strict failed pixel and magnified example. `--strict` requests the
old strict exit status. Current binary/capture hashes and fresh platform runs
belong in the next acceptance evidence record; older Metal/Linux raw captures
are historical observations and are not fresh builds of this revision.
