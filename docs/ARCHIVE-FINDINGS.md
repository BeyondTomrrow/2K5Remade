# Archive discovery checkpoint

Native C tool: tools/asset_index.c. Build with tools/build.ps1; run build/Release/NFL2K5_AssetIndex.exe. It reads the existing extraction and writes analysis/archive-index.csv. Originals are opened read-only.

Confirmed: archive 0 starts with entry count 4323 at byte 0, bank count 16 at byte 8, and 16 sector counts at byte 12. Every sector count multiplied by 2048 exactly matches its corresponding file size. The 12-byte record table starts at byte 156 (not 160).

Current interpretation: records contain an identifier, packed size/flags, and a sector address in the concatenated archive stream. Mapping that sector address through cumulative bank sizes and masking size to the low 28 bits gives 4310 entries contained within a bank; 13 require investigation (possible cross-bank payloads or size flags). This is an exploratory index, not a fully validated extractor. Exit status 2 reports those unresolved records. Full payload decoding has not been implemented.

Entry 0: identifier EDD549BD, bank 0, local offset 53248, candidate size 789984. Payload begins HITX and includes TXTR and UTF-16LE legalpage near its start. Entry 6 includes logos.cdf and a build-path string. These are actual local file observations, not generated artwork.

Next short burst: parse entry 0's HITX/TXTR records, determine dimensions/format and texture-data offsets, decode its legal-page texture, and display it in the native window. Do not claim the current moving-ball demo renders original assets: it only checks file sizes. Preserve the existing translated-C and reconstruction builds.

Completed next step: legalpage visually verified. First HITX block starts at archive byte 53248. Header length field at +8 is 128; there is an additional 32-byte prefix, so indices begin at block +160. Xbox format at +96 is 09910B29: P8, 512x512. Palette begins at +160+262144, contains 256 BGRA entries. Morton unswizzle gives readable NFL/Players Inc. legal artwork. Next HITX starts at +32+0x40480. Native decoder and window rendering are implemented; exported verification image is analysis/legalpage.png. The earlier instruction about the demo checking only sizes describes the prior build.
