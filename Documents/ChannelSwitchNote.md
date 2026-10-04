# Channel Switch Note for NanaZip Rev development

P.S. Only for the maintainers of this fork.

The fork ships a single store identity, so the release and the preview channel
only differ in the shell extension CLSID and in the icon assets. The package
identity, the publisher, and every displayed name are shared.

## Preview

- 77EFEFF7-7FA2-479D-8390-B1E940488DC1
- <Content Include="..\Assets\PreviewPackageAssets\**\*">
- Assets/NanaZipPreview.ico
- Assets/NanaZipPreviewSfx.ico

## Stable

- 99D03F31-2E96-4D91-81DD-E378A99C06B8
- <Content Include="..\Assets\PackageAssets\**\*">
- Assets/NanaZip.ico
- Assets/NanaZipSfx.ico

## Shared

- DisplayName="NanaZip Rev"
- Name="Kaesinol.NanaZipRev"
- Publisher="CN=23EBD860-A383-4AEA-836A-B041352C93FF"
- <DisplayName>NanaZip Rev</DisplayName>
- <PublisherDisplayName>Kaesinol</PublisherDisplayName>
- return ::SHStrDupW(L"NanaZip Rev", ppszName);
