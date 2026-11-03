// ArmpiStorage.cs -- les dialogues de fichiers d'Avalonia sur ceux d'UIKit (ui_file_open, ui_file_save,
// ui_folder_choose), un fichier a la fois. Ces dialogues tournent dans leur propre boucle : le programme
// attend leur fin, comme un dialogue modal d'UIKit.
#nullable enable
using System;
using System.Collections.Generic;
using System.IO;
using System.Kits;
using System.Linq;
using System.Threading.Tasks;
using System.UIKit;
using Avalonia.Platform.Storage;
using Avalonia.Platform.Storage.FileIO;

namespace Avalonia.Armpi
{
	internal sealed unsafe class ArmpiStorage : BclStorageProvider
	{
		const int PathMax = 512;
		readonly ArmpiWindow _window;

		public ArmpiStorage (ArmpiWindow window) => _window = window;

		public override bool CanOpen => true;
		public override bool CanSave => true;
		public override bool CanPickFolder => true;

		/// <summary>.NET ne connait que les chemins Unix : le volume du systeme s'efface, comme dans SystemNative_GetCwd.</summary>
		static string ToNet (string path)
		{
			int c = path.IndexOf (":/", StringComparison.Ordinal);
			return c > 0 && path.StartsWith ("SD0:", StringComparison.OrdinalIgnoreCase) ? path.Substring (c + 1) : path;
		}

		static string? Patterns (IReadOnlyList<FilePickerFileType>? types)
		{
			var all = types?.Where (t => t.Patterns != null).SelectMany (t => t.Patterns!).ToArray ();
			return all == null || all.Length == 0 || all.Contains ("*") || all.Contains ("*.*") ? null : string.Join (";", all);
		}

		/// <summary>Le dialogue, la fenetre rendue courante et le filtre pose ; le chemin choisi, ou null.</summary>
		string? Ask (PickerOptions options, string? filter, Func<long, long, long> dialog)
		{
			string? result = null;
			_window.InWindow (() =>
			{
				byte* b = stackalloc byte[PathMax];
				long d = options.SuggestedStartLocation?.TryGetLocalPath () is { } start ? KitLoader.Utf8 (start) : 0;
				long f = filter == null ? 0 : KitLoader.Utf8 (filter);
				UI.ui_file_filter (f);
				long r = dialog (d, (long) b);
				UI.ui_file_filter (0);
				if (f != 0) KitLoader.Free (f);
				if (d != 0) KitLoader.Free (d);
				if (r != 0)
					result = ToNet (KitLoader.String ((long) b));
			});
			return result;
		}

		public override Task<OpenFilePickerResult> OpenFilePickerWithResultAsync (FilePickerOpenOptions options)
		{
			var path = Ask (options, Patterns (options.FileTypeFilter), (d, b) => UI.ui_file_open (d, b, PathMax));
			return Task.FromResult (new OpenFilePickerResult
			{
				Files = path == null ? Array.Empty<IStorageFile> () : new IStorageFile[] { new BclStorageFile (new FileInfo (path)) },
			});
		}

		public override Task<SaveFilePickerResult> SaveFilePickerWithResultAsync (FilePickerSaveOptions options)
		{
			var path = Ask (options, Patterns (options.FileTypeChoices), (d, b) =>
				Ui.With (options.SuggestedFileName ?? "", n => UI.ui_file_save (d, n, b, PathMax)));
			return Task.FromResult (new SaveFilePickerResult { File = path == null ? null : new BclStorageFile (new FileInfo (path)) });
		}

		public override Task<IReadOnlyList<IStorageFolder>> OpenFolderPickerAsync (FolderPickerOpenOptions options)
		{
			string? path = null;
			_window.InWindow (() =>
			{
				byte* b = stackalloc byte[PathMax];
				if (UI.ui_folder_choose (0, (long) b, PathMax) != 0)
					path = ToNet (KitLoader.String ((long) b));
			});
			IReadOnlyList<IStorageFolder> folders = path == null ? Array.Empty<IStorageFolder> () : new IStorageFolder[] { new BclStorageFolder (new DirectoryInfo (path)) };
			return Task.FromResult (folders);
		}
	}
}
