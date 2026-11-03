// Layout.cs -- ce que la signature des membres de WinForms impose d'avoir aux memes noms (docs/WINFORMS-PORTABLE.md).
#nullable disable
using System.Collections;
using System.Collections.Generic;
using System.Windows.Forms;

namespace System.Windows.Forms.Layout;

public class ArrangedElementCollection : IEnumerable
{
	internal readonly List<Control> Items = new ();
	public virtual int Count => Items.Count;
	public virtual IEnumerator GetEnumerator () => Items.GetEnumerator ();
}
