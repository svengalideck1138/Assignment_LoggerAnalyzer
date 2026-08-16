namespace Individual_Assignment01_UI
{
    partial class Form1
    {
        /// <summary>
        /// 필수 디자이너 변수입니다.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// 사용 중인 모든 리소스를 정리합니다.
        /// </summary>
        /// <param name="disposing">관리되는 리소스를 삭제해야 하면 true이고, 그렇지 않으면 false입니다.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form 디자이너에서 생성한 코드

        /// <summary>
        /// 디자이너 지원에 필요한 메서드입니다.
        /// 이 메서드의 내용을 코드 편집기로 수정하지 마세요.
        /// </summary>
        private void InitializeComponent()
        {
            this.GroupBox_Step1 = new System.Windows.Forms.GroupBox();
            this.Lbl_IpCaption = new System.Windows.Forms.Label();
            this.Tbox_TargetIP = new System.Windows.Forms.TextBox();
            this.label1 = new System.Windows.Forms.Label();
            this.Tbox_TargetPort = new System.Windows.Forms.TextBox();
            this.Btn_SOCKETConnect = new System.Windows.Forms.Button();
            this.pictureBox_SocketLED = new System.Windows.Forms.PictureBox();
            this.GroupBox_Step2 = new System.Windows.Forms.GroupBox();
            this.textBox_FileLocation = new System.Windows.Forms.TextBox();
            this.Lbl_FileSize = new System.Windows.Forms.Label();
            this.Btn_Open = new System.Windows.Forms.Button();
            this.GroupBox_Step3 = new System.Windows.Forms.GroupBox();
            this.Btn_Send = new System.Windows.Forms.Button();
            this.Btn_Cancel = new System.Windows.Forms.Button();
            this.Lbl_AnalyzeCaption = new System.Windows.Forms.Label();
            this.Pbar_Analyze = new System.Windows.Forms.ProgressBar();
            this.Lbl_AnalyzePct = new System.Windows.Forms.Label();
            this.GroupBox_Step4 = new System.Windows.Forms.GroupBox();
            this.Lbl_SectionCaption = new System.Windows.Forms.Label();
            this.Cbo_Section = new System.Windows.Forms.ComboBox();
            this.Btn_Download = new System.Windows.Forms.Button();
            this.Grid_Result = new System.Windows.Forms.DataGridView();
            this.GroupBox_Log = new System.Windows.Forms.GroupBox();
            this.Tbox_Log = new System.Windows.Forms.TextBox();
            this.statusStrip1 = new System.Windows.Forms.StatusStrip();
            this.Lbl_Status = new System.Windows.Forms.ToolStripStatusLabel();
            this.Lbl_StatusSpeed = new System.Windows.Forms.ToolStripStatusLabel();
            this.Lbl_StatusElapsed = new System.Windows.Forms.ToolStripStatusLabel();
            this.openFileDialog1 = new System.Windows.Forms.OpenFileDialog();
            this.saveFileDialog1 = new System.Windows.Forms.SaveFileDialog();
            this.GroupBox_Step1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.pictureBox_SocketLED)).BeginInit();
            this.GroupBox_Step2.SuspendLayout();
            this.GroupBox_Step3.SuspendLayout();
            this.GroupBox_Step4.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.Grid_Result)).BeginInit();
            this.GroupBox_Log.SuspendLayout();
            this.statusStrip1.SuspendLayout();
            this.SuspendLayout();
            // 
            // GroupBox_Step1
            // 
            this.GroupBox_Step1.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.GroupBox_Step1.Controls.Add(this.Lbl_IpCaption);
            this.GroupBox_Step1.Controls.Add(this.Tbox_TargetIP);
            this.GroupBox_Step1.Controls.Add(this.label1);
            this.GroupBox_Step1.Controls.Add(this.Tbox_TargetPort);
            this.GroupBox_Step1.Controls.Add(this.Btn_SOCKETConnect);
            this.GroupBox_Step1.Controls.Add(this.pictureBox_SocketLED);
            this.GroupBox_Step1.Location = new System.Drawing.Point(8, 8);
            this.GroupBox_Step1.Name = "GroupBox_Step1";
            this.GroupBox_Step1.Size = new System.Drawing.Size(579, 52);
            this.GroupBox_Step1.TabIndex = 0;
            this.GroupBox_Step1.TabStop = false;
            this.GroupBox_Step1.Text = "1. Server 설정";
            // 
            // Lbl_IpCaption
            // 
            this.Lbl_IpCaption.Location = new System.Drawing.Point(6, 21);
            this.Lbl_IpCaption.Name = "Lbl_IpCaption";
            this.Lbl_IpCaption.Size = new System.Drawing.Size(60, 15);
            this.Lbl_IpCaption.TabIndex = 20;
            this.Lbl_IpCaption.Text = "Server IP";
            this.Lbl_IpCaption.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
            // 
            // Tbox_TargetIP
            // 
            this.Tbox_TargetIP.Location = new System.Drawing.Point(72, 17);
            this.Tbox_TargetIP.Name = "Tbox_TargetIP";
            this.Tbox_TargetIP.Size = new System.Drawing.Size(115, 23);
            this.Tbox_TargetIP.TabIndex = 1;
            this.Tbox_TargetIP.Text = "127.0.0.1";
            this.Tbox_TargetIP.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            // 
            // label1
            // 
            this.label1.Location = new System.Drawing.Point(193, 21);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(70, 15);
            this.label1.TabIndex = 34;
            this.label1.Text = "Server Port";
            this.label1.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
            // 
            // Tbox_TargetPort
            // 
            this.Tbox_TargetPort.Location = new System.Drawing.Point(269, 17);
            this.Tbox_TargetPort.MaxLength = 5;
            this.Tbox_TargetPort.Name = "Tbox_TargetPort";
            this.Tbox_TargetPort.Size = new System.Drawing.Size(40, 23);
            this.Tbox_TargetPort.TabIndex = 2;
            this.Tbox_TargetPort.Text = "8088";
            this.Tbox_TargetPort.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            // 
            // Btn_SOCKETConnect
            // 
            this.Btn_SOCKETConnect.Location = new System.Drawing.Point(315, 15);
            this.Btn_SOCKETConnect.Name = "Btn_SOCKETConnect";
            this.Btn_SOCKETConnect.Size = new System.Drawing.Size(89, 26);
            this.Btn_SOCKETConnect.TabIndex = 3;
            this.Btn_SOCKETConnect.Text = "Connect";
            this.Btn_SOCKETConnect.UseVisualStyleBackColor = true;
            this.Btn_SOCKETConnect.Click += new System.EventHandler(this.Btn_SOCKETConnect_Click);
            // 
            // pictureBox_SocketLED
            // 
            this.pictureBox_SocketLED.Image = global::Individual_Assignment01_UI.Properties.Resources.LedTransparent;
            this.pictureBox_SocketLED.Location = new System.Drawing.Point(410, 20);
            this.pictureBox_SocketLED.Name = "pictureBox_SocketLED";
            this.pictureBox_SocketLED.Size = new System.Drawing.Size(16, 16);
            this.pictureBox_SocketLED.SizeMode = System.Windows.Forms.PictureBoxSizeMode.AutoSize;
            this.pictureBox_SocketLED.TabIndex = 22;
            this.pictureBox_SocketLED.TabStop = false;
            // 
            // GroupBox_Step2
            // 
            this.GroupBox_Step2.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.GroupBox_Step2.Controls.Add(this.textBox_FileLocation);
            this.GroupBox_Step2.Controls.Add(this.Lbl_FileSize);
            this.GroupBox_Step2.Controls.Add(this.Btn_Open);
            this.GroupBox_Step2.Location = new System.Drawing.Point(8, 64);
            this.GroupBox_Step2.Name = "GroupBox_Step2";
            this.GroupBox_Step2.Size = new System.Drawing.Size(579, 52);
            this.GroupBox_Step2.TabIndex = 1;
            this.GroupBox_Step2.TabStop = false;
            this.GroupBox_Step2.Text = "2. Log 파일 선택";
            // 
            // textBox_FileLocation
            // 
            this.textBox_FileLocation.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.textBox_FileLocation.Location = new System.Drawing.Point(6, 17);
            this.textBox_FileLocation.Name = "textBox_FileLocation";
            this.textBox_FileLocation.ReadOnly = true;
            this.textBox_FileLocation.Size = new System.Drawing.Size(353, 23);
            this.textBox_FileLocation.TabIndex = 4;
            // 
            // Lbl_FileSize
            // 
            this.Lbl_FileSize.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.Lbl_FileSize.ForeColor = System.Drawing.SystemColors.GrayText;
            this.Lbl_FileSize.Location = new System.Drawing.Point(365, 21);
            this.Lbl_FileSize.Name = "Lbl_FileSize";
            this.Lbl_FileSize.Size = new System.Drawing.Size(96, 15);
            this.Lbl_FileSize.TabIndex = 24;
            this.Lbl_FileSize.Text = "(no file selected)";
            this.Lbl_FileSize.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
            // 
            // Btn_Open
            // 
            this.Btn_Open.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.Btn_Open.Location = new System.Drawing.Point(469, 16);
            this.Btn_Open.Name = "Btn_Open";
            this.Btn_Open.Size = new System.Drawing.Size(104, 25);
            this.Btn_Open.TabIndex = 5;
            this.Btn_Open.Text = "Open...";
            this.Btn_Open.UseVisualStyleBackColor = true;
            this.Btn_Open.Click += new System.EventHandler(this.Btn_Open_Click);
            // 
            // GroupBox_Step3
            // 
            this.GroupBox_Step3.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.GroupBox_Step3.Controls.Add(this.Btn_Send);
            this.GroupBox_Step3.Controls.Add(this.Btn_Cancel);
            this.GroupBox_Step3.Controls.Add(this.Lbl_AnalyzeCaption);
            this.GroupBox_Step3.Controls.Add(this.Pbar_Analyze);
            this.GroupBox_Step3.Controls.Add(this.Lbl_AnalyzePct);
            this.GroupBox_Step3.Location = new System.Drawing.Point(8, 120);
            this.GroupBox_Step3.Name = "GroupBox_Step3";
            this.GroupBox_Step3.Size = new System.Drawing.Size(579, 76);
            this.GroupBox_Step3.TabIndex = 2;
            this.GroupBox_Step3.TabStop = false;
            this.GroupBox_Step3.Text = "3. 전송 및 분석";
            // 
            // Btn_Send
            // 
            this.Btn_Send.Enabled = false;
            this.Btn_Send.Location = new System.Drawing.Point(6, 18);
            this.Btn_Send.Name = "Btn_Send";
            this.Btn_Send.Size = new System.Drawing.Size(140, 28);
            this.Btn_Send.TabIndex = 6;
            this.Btn_Send.Text = "Send to Server";
            this.Btn_Send.UseVisualStyleBackColor = true;
            this.Btn_Send.Click += new System.EventHandler(this.Btn_Send_Click);
            // 
            // Btn_Cancel
            // 
            this.Btn_Cancel.Enabled = false;
            this.Btn_Cancel.Location = new System.Drawing.Point(152, 18);
            this.Btn_Cancel.Name = "Btn_Cancel";
            this.Btn_Cancel.Size = new System.Drawing.Size(80, 28);
            this.Btn_Cancel.TabIndex = 7;
            this.Btn_Cancel.Text = "Cancel";
            this.Btn_Cancel.UseVisualStyleBackColor = true;
            this.Btn_Cancel.Click += new System.EventHandler(this.Btn_Cancel_Click);
            // 
            // Lbl_AnalyzeCaption
            // 
            this.Lbl_AnalyzeCaption.Location = new System.Drawing.Point(6, 56);
            this.Lbl_AnalyzeCaption.Name = "Lbl_AnalyzeCaption";
            this.Lbl_AnalyzeCaption.Size = new System.Drawing.Size(52, 15);
            this.Lbl_AnalyzeCaption.TabIndex = 28;
            this.Lbl_AnalyzeCaption.Text = "Progress";
            this.Lbl_AnalyzeCaption.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
            // 
            // Pbar_Analyze
            // 
            this.Pbar_Analyze.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.Pbar_Analyze.Location = new System.Drawing.Point(62, 55);
            this.Pbar_Analyze.Maximum = 1000;
            this.Pbar_Analyze.Name = "Pbar_Analyze";
            this.Pbar_Analyze.Size = new System.Drawing.Size(441, 16);
            this.Pbar_Analyze.TabIndex = 29;
            // 
            // Lbl_AnalyzePct
            // 
            this.Lbl_AnalyzePct.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.Lbl_AnalyzePct.Location = new System.Drawing.Point(509, 55);
            this.Lbl_AnalyzePct.Name = "Lbl_AnalyzePct";
            this.Lbl_AnalyzePct.Size = new System.Drawing.Size(62, 16);
            this.Lbl_AnalyzePct.TabIndex = 30;
            this.Lbl_AnalyzePct.Text = "0.0 %";
            this.Lbl_AnalyzePct.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
            // 
            // GroupBox_Step4
            // 
            this.GroupBox_Step4.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.GroupBox_Step4.Controls.Add(this.Lbl_SectionCaption);
            this.GroupBox_Step4.Controls.Add(this.Cbo_Section);
            this.GroupBox_Step4.Controls.Add(this.Btn_Download);
            this.GroupBox_Step4.Controls.Add(this.Grid_Result);
            this.GroupBox_Step4.Location = new System.Drawing.Point(8, 202);
            this.GroupBox_Step4.Name = "GroupBox_Step4";
            this.GroupBox_Step4.Size = new System.Drawing.Size(579, 152);
            this.GroupBox_Step4.TabIndex = 3;
            this.GroupBox_Step4.TabStop = false;
            this.GroupBox_Step4.Text = "4. 분석 결과";
            // 
            // Lbl_SectionCaption
            // 
            this.Lbl_SectionCaption.Location = new System.Drawing.Point(6, 20);
            this.Lbl_SectionCaption.Name = "Lbl_SectionCaption";
            this.Lbl_SectionCaption.Size = new System.Drawing.Size(50, 15);
            this.Lbl_SectionCaption.TabIndex = 31;
            this.Lbl_SectionCaption.Text = "Section";
            this.Lbl_SectionCaption.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
            // 
            // Cbo_Section
            // 
            this.Cbo_Section.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.Cbo_Section.Enabled = false;
            this.Cbo_Section.FormattingEnabled = true;
            this.Cbo_Section.Location = new System.Drawing.Point(62, 17);
            this.Cbo_Section.Name = "Cbo_Section";
            this.Cbo_Section.Size = new System.Drawing.Size(180, 23);
            this.Cbo_Section.TabIndex = 8;
            this.Cbo_Section.SelectedIndexChanged += new System.EventHandler(this.Cbo_Section_SelectedIndexChanged);
            // 
            // Btn_Download
            // 
            this.Btn_Download.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.Btn_Download.Enabled = false;
            this.Btn_Download.Location = new System.Drawing.Point(469, 16);
            this.Btn_Download.Name = "Btn_Download";
            this.Btn_Download.Size = new System.Drawing.Size(104, 25);
            this.Btn_Download.TabIndex = 9;
            this.Btn_Download.Text = "Save result.csv";
            this.Btn_Download.UseVisualStyleBackColor = true;
            this.Btn_Download.Click += new System.EventHandler(this.Btn_Download_Click);
            // 
            // Grid_Result
            // 
            this.Grid_Result.AllowUserToAddRows = false;
            this.Grid_Result.AllowUserToDeleteRows = false;
            this.Grid_Result.AllowUserToResizeRows = false;
            this.Grid_Result.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.Grid_Result.AutoSizeColumnsMode = System.Windows.Forms.DataGridViewAutoSizeColumnsMode.Fill;
            this.Grid_Result.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            this.Grid_Result.Location = new System.Drawing.Point(6, 46);
            this.Grid_Result.Name = "Grid_Result";
            this.Grid_Result.ReadOnly = true;
            this.Grid_Result.RowHeadersVisible = false;
            this.Grid_Result.RowTemplate.Height = 23;
            this.Grid_Result.SelectionMode = System.Windows.Forms.DataGridViewSelectionMode.FullRowSelect;
            this.Grid_Result.Size = new System.Drawing.Size(567, 100);
            this.Grid_Result.TabIndex = 10;
            // 
            // GroupBox_Log
            // 
            this.GroupBox_Log.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.GroupBox_Log.Controls.Add(this.Tbox_Log);
            this.GroupBox_Log.Location = new System.Drawing.Point(8, 360);
            this.GroupBox_Log.Name = "GroupBox_Log";
            this.GroupBox_Log.Size = new System.Drawing.Size(579, 170);
            this.GroupBox_Log.TabIndex = 4;
            this.GroupBox_Log.TabStop = false;
            this.GroupBox_Log.Text = "Log";
            // 
            // Tbox_Log
            // 
            this.Tbox_Log.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.Tbox_Log.BackColor = System.Drawing.SystemColors.Window;
            this.Tbox_Log.Font = new System.Drawing.Font("Consolas", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.Tbox_Log.Location = new System.Drawing.Point(6, 20);
            this.Tbox_Log.Multiline = true;
            this.Tbox_Log.Name = "Tbox_Log";
            this.Tbox_Log.ReadOnly = true;
            this.Tbox_Log.ScrollBars = System.Windows.Forms.ScrollBars.Both;
            this.Tbox_Log.Size = new System.Drawing.Size(567, 144);
            this.Tbox_Log.TabIndex = 11;
            // 
            // statusStrip1
            // 
            this.statusStrip1.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.Lbl_Status,
            this.Lbl_StatusSpeed,
            this.Lbl_StatusElapsed});
            this.statusStrip1.Location = new System.Drawing.Point(0, 538);
            this.statusStrip1.Name = "statusStrip1";
            this.statusStrip1.Size = new System.Drawing.Size(595, 22);
            this.statusStrip1.TabIndex = 33;
            // 
            // Lbl_Status
            // 
            this.Lbl_Status.Name = "Lbl_Status";
            this.Lbl_Status.Size = new System.Drawing.Size(572, 17);
            this.Lbl_Status.Spring = true;
            this.Lbl_Status.Text = "Disconnected";
            this.Lbl_Status.TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
            // 
            // Lbl_StatusSpeed
            // 
            this.Lbl_StatusSpeed.BorderSides = System.Windows.Forms.ToolStripStatusLabelBorderSides.Left;
            this.Lbl_StatusSpeed.BorderStyle = System.Windows.Forms.Border3DStyle.Etched;
            this.Lbl_StatusSpeed.Name = "Lbl_StatusSpeed";
            this.Lbl_StatusSpeed.Size = new System.Drawing.Size(4, 17);
            // 
            // Lbl_StatusElapsed
            // 
            this.Lbl_StatusElapsed.BorderSides = System.Windows.Forms.ToolStripStatusLabelBorderSides.Left;
            this.Lbl_StatusElapsed.BorderStyle = System.Windows.Forms.Border3DStyle.Etched;
            this.Lbl_StatusElapsed.Name = "Lbl_StatusElapsed";
            this.Lbl_StatusElapsed.Size = new System.Drawing.Size(4, 17);
            // 
            // openFileDialog1
            // 
            this.openFileDialog1.Filter = "Log files (*.log)|*.log|Text files (*.txt)|*.txt|All files (*.*)|*.*";
            this.openFileDialog1.Title = "Select a log file to upload";
            // 
            // saveFileDialog1
            // 
            this.saveFileDialog1.DefaultExt = "csv";
            this.saveFileDialog1.FileName = "result.csv";
            this.saveFileDialog1.Filter = "CSV files (*.csv)|*.csv|All files (*.*)|*.*";
            this.saveFileDialog1.Title = "Save analysis result";
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(595, 560);
            this.Controls.Add(this.statusStrip1);
            this.Controls.Add(this.GroupBox_Log);
            this.Controls.Add(this.GroupBox_Step4);
            this.Controls.Add(this.GroupBox_Step3);
            this.Controls.Add(this.GroupBox_Step2);
            this.Controls.Add(this.GroupBox_Step1);
            this.Font = new System.Drawing.Font("Segoe UI", 9F);
            this.MinimumSize = new System.Drawing.Size(472, 480);
            this.Name = "Form1";
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
            this.Text = "Zhenyu_LoggerAnalyzer - Windows Client";
            this.FormClosing += new System.Windows.Forms.FormClosingEventHandler(this.Form1_FormClosing);
            this.GroupBox_Step1.ResumeLayout(false);
            this.GroupBox_Step1.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this.pictureBox_SocketLED)).EndInit();
            this.GroupBox_Step2.ResumeLayout(false);
            this.GroupBox_Step2.PerformLayout();
            this.GroupBox_Step3.ResumeLayout(false);
            this.GroupBox_Step4.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.Grid_Result)).EndInit();
            this.GroupBox_Log.ResumeLayout(false);
            this.GroupBox_Log.PerformLayout();
            this.statusStrip1.ResumeLayout(false);
            this.statusStrip1.PerformLayout();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.GroupBox GroupBox_Step1;
        private System.Windows.Forms.Label Lbl_IpCaption;
        private System.Windows.Forms.TextBox Tbox_TargetIP;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.TextBox Tbox_TargetPort;
        private System.Windows.Forms.Button Btn_SOCKETConnect;
        private System.Windows.Forms.PictureBox pictureBox_SocketLED;
        private System.Windows.Forms.GroupBox GroupBox_Step2;
        private System.Windows.Forms.TextBox textBox_FileLocation;
        private System.Windows.Forms.Label Lbl_FileSize;
        private System.Windows.Forms.Button Btn_Open;
        private System.Windows.Forms.GroupBox GroupBox_Step3;
        private System.Windows.Forms.Button Btn_Send;
        private System.Windows.Forms.Button Btn_Cancel;
        private System.Windows.Forms.Label Lbl_AnalyzeCaption;
        private System.Windows.Forms.ProgressBar Pbar_Analyze;
        private System.Windows.Forms.Label Lbl_AnalyzePct;
        private System.Windows.Forms.GroupBox GroupBox_Step4;
        private System.Windows.Forms.Label Lbl_SectionCaption;
        private System.Windows.Forms.ComboBox Cbo_Section;
        private System.Windows.Forms.Button Btn_Download;
        private System.Windows.Forms.DataGridView Grid_Result;
        private System.Windows.Forms.GroupBox GroupBox_Log;
        private System.Windows.Forms.TextBox Tbox_Log;
        private System.Windows.Forms.StatusStrip statusStrip1;
        private System.Windows.Forms.ToolStripStatusLabel Lbl_Status;
        private System.Windows.Forms.ToolStripStatusLabel Lbl_StatusSpeed;
        private System.Windows.Forms.ToolStripStatusLabel Lbl_StatusElapsed;
        private System.Windows.Forms.OpenFileDialog openFileDialog1;
        private System.Windows.Forms.SaveFileDialog saveFileDialog1;
    }
}
