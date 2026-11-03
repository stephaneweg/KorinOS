namespace WfHello;

partial class MainForm
{
	private System.ComponentModel.IContainer components = null;

	protected override void Dispose (bool disposing)
	{
		if (disposing && (components != null))
			components.Dispose ();
		base.Dispose (disposing);
	}

	private void InitializeComponent ()
	{
		components = new System.ComponentModel.Container ();
		osLabel = new System.Windows.Forms.Label ();
		asmLabel = new System.Windows.Forms.Label ();
		name = new System.Windows.Forms.TextBox ();
		add = new System.Windows.Forms.Button ();
		check = new System.Windows.Forms.CheckBox ();
		list = new System.Windows.Forms.ListBox ();
		canvas = new System.Windows.Forms.Panel ();
		timer = new System.Windows.Forms.Timer (components);
		SuspendLayout ();
		//
		// osLabel
		//
		osLabel.AutoSize = true;
		osLabel.Location = new System.Drawing.Point (12, 12);
		osLabel.Name = "osLabel";
		osLabel.Size = new System.Drawing.Size (440, 24);
		osLabel.TabIndex = 0;
		osLabel.Text = "os";
		//
		// asmLabel
		//
		asmLabel.AutoSize = true;
		asmLabel.Location = new System.Drawing.Point (12, 38);
		asmLabel.Name = "asmLabel";
		asmLabel.Size = new System.Drawing.Size (440, 24);
		asmLabel.TabIndex = 1;
		asmLabel.Text = "assembly";
		//
		// name
		//
		name.Location = new System.Drawing.Point (12, 72);
		name.Margin = new System.Windows.Forms.Padding (3, 4, 3, 4);
		name.Name = "name";
		name.Size = new System.Drawing.Size (200, 27);
		name.TabIndex = 2;
		name.Text = "bonjour";
		//
		// add
		//
		add.Location = new System.Drawing.Point (220, 72);
		add.Name = "add";
		add.Size = new System.Drawing.Size (94, 29);
		add.TabIndex = 3;
		add.Text = "Ajouter";
		add.UseVisualStyleBackColor = true;
		add.Click += OnAdd;
		//
		// check
		//
		check.AutoSize = true;
		check.Location = new System.Drawing.Point (324, 74);
		check.Name = "check";
		check.Size = new System.Drawing.Size (100, 24);
		check.TabIndex = 4;
		check.Text = "Bleu";
		check.UseVisualStyleBackColor = true;
		check.CheckedChanged += OnChecked;
		//
		// list
		//
		list.FormattingEnabled = true;
		list.Items.AddRange (new object[] { "premier" });
		list.Location = new System.Drawing.Point (12, 112);
		list.Name = "list";
		list.Size = new System.Drawing.Size (200, 180);
		list.TabIndex = 5;
		//
		// canvas
		//
		canvas.Location = new System.Drawing.Point (220, 112);
		canvas.Name = "canvas";
		canvas.Size = new System.Drawing.Size (320, 180);
		canvas.TabIndex = 6;
		canvas.Paint += OnPaintCanvas;
		//
		// timer
		//
		timer.Interval = 500;
		timer.Tick += OnTick;
		//
		// MainForm
		//
		AcceptButton = add;
		AutoScaleDimensions = new System.Drawing.SizeF (8F, 20F);
		AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
		ClientSize = new System.Drawing.Size (556, 308);
		Controls.Add (canvas);
		Controls.Add (list);
		Controls.Add (check);
		Controls.Add (add);
		Controls.Add (name);
		Controls.Add (asmLabel);
		Controls.Add (osLabel);
		Name = "MainForm";
		Text = "wfhello";
		ResumeLayout (false);
		PerformLayout ();
	}

	private System.Windows.Forms.Label osLabel;
	private System.Windows.Forms.Label asmLabel;
	private System.Windows.Forms.TextBox name;
	private System.Windows.Forms.Button add;
	private System.Windows.Forms.CheckBox check;
	private System.Windows.Forms.ListBox list;
	private System.Windows.Forms.Panel canvas;
	private System.Windows.Forms.Timer timer;
}
