using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;
using Mile.DotNet.Helpers;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace NanaZip.Build.Tasks
{
    public class RefreshProjectResources : Task
    {
        [Required]
        public string? RootPath { get; set; }

        [Required]
        public bool BuildPreviewRelease { get; set; }

        static void ReplaceFileContentViaStringList(
            string FilePath,
            List<string> FromList,
            List<string> ToList)
        {
            if (FromList.Count != ToList.Count)
            {
                throw new ArgumentOutOfRangeException();
            }

            string Content = File.ReadAllText(FilePath, Encoding.UTF8);

            for (int Index = 0; Index < FromList.Count; ++Index)
            {
                Content = Content.Replace(FromList[Index], ToList[Index]);
            }

            if (Path.GetExtension(FilePath) == ".rc")
            {
                File.WriteAllText(FilePath, Content, Encoding.Unicode);
            }
            else
            {
                Text.SaveTextToFileAsUtf8WithBom(FilePath, Content);
            }
        }

        static List<string> ReleaseStringList = new List<string>
        {
            "DisplayName=\"NanaZip Rev\"",
            "Name=\"Kaesinol.NanaZipRev\"",
            "<DisplayName>NanaZip Rev</DisplayName>",
            "99D03F31-2E96-4D91-81DD-E378A99C06B8",
            "return ::SHStrDupW(L\"NanaZip Rev\", ppszName);",
            "<Content Include=\"..\\Assets\\PackageAssets\\**\\*\">",
            "Assets/NanaZip.ico",
            "Assets/NanaZipSfx.ico",
        };

        static List<string> PreviewStringList = new List<string>
        {
            "DisplayName=\"NanaZip Rev\"",
            "Name=\"Kaesinol.NanaZipRev\"",
            "<DisplayName>NanaZip Rev</DisplayName>",
            "77EFEFF7-7FA2-479D-8390-B1E940488DC1",
            "return ::SHStrDupW(L\"NanaZip Rev\", ppszName);",
            "<Content Include=\"..\\Assets\\PreviewPackageAssets\\**\\*\">",
            "Assets/NanaZipPreview.ico",
            "Assets/NanaZipPreviewSfx.ico",
        };

        static List<string> FileList = new List<string>
        {
            @"{0}\NanaZip.Core\SevenZip\CPP\7zip\Bundles\SFXCon\resource.rc",
            @"{0}\NanaZip.Core\SevenZip\CPP\7zip\Bundles\SFXSetup\resource.rc",
            @"{0}\NanaZip.Core\SevenZip\CPP\7zip\Bundles\SFXWin\resource.rc",
            @"{0}\NanaZip.Universal\SevenZip\CPP\7zip\UI\GUI\resource.rc",
            @"{0}\NanaZip.UI.Modern\SevenZip\CPP\7zip\UI\FileManager\resource.rc",
            @"{0}\NanaZip.UI.Modern\NanaZip.ShellExtension.cpp",
            @"{0}\NanaZipPackage\Package.appxmanifest",
            @"{0}\NanaZipPackage\NanaZipPackage.wapproj",
        };

        public override bool Execute()
        {
            foreach (var FilePath in FileList)
            {
                ReplaceFileContentViaStringList(
                    string.Format(FilePath, RootPath),
                    BuildPreviewRelease ? ReleaseStringList : PreviewStringList,
                    BuildPreviewRelease ? PreviewStringList : ReleaseStringList);
            }

            return true;
        }
    }
}
