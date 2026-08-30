using System;
using System.Collections.Generic;
using System.Data;
using System.Globalization;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

using Individual_Assignment01_UI.Network;

namespace Individual_Assignment01_UI
{
    public partial class Form1 : Form
    {
        // 연결 하나를 소유한다. 끊을 때 Dispose 로 소켓을 즉시 반환한다.
        // 구체 타입이 아니라 인터페이스로만 연결을 다룬다.
        // 생성은 ConnectionFactory 가 담당한다.
        private IServerConnection _conn;

        // 상태바의 속도/경과 세그먼트가 참조하는 전송 시계.
        private System.Diagnostics.Stopwatch _uploadClock;

        // 진행 중인 비동기 작업을 중단시키기 위한 토큰 소스.
        private CancellationTokenSource _cts;

        private bool _busy;              // 연결/해제가 진행 중
        private bool _uploading;         // 파일 전송이 진행 중
        private bool _closeRequested;    // 전송 중 창 닫기를 눌렀다
        private CancellationTokenSource _uploadCts;
        private string _selectedFile;    // 업로드 대상 경로
        private long _selectedFileSize;

        private string _resultCsv = string.Empty;         // 서버가 보낸 result.csv 원문
        private List<CsvSection> _sections;               // 섹션 단위로 쪼갠 것

        private DataTable _liveTable;                     // 전송 중 실시간 결과
        private int _liveRevision = -1;                   // 마지막으로 그린 서버 통계 버전

        // System.Threading.Timer 와 이름이 겹치므로 정확히 지정한다.
        // UI 컨트롤을 건드리므로 반드시 WinForms 타이머여야 한다 - 이쪽은
        // 메시지 펌프에서 Tick 이 오므로 Invoke 없이 컨트롤을 만질 수 있다.
        private readonly System.Windows.Forms.Timer _ledTimer =
            new System.Windows.Forms.Timer();
        private LedState _ledState = LedState.Idle;
        private bool _ledOn;

        // 유휴 상태의 회선 감시. TCP 는 서버가 죽어도 통보해 주지 않아서,
        // 아무 I/O 도 없는 동안에는 이 타이머가 1초마다 소켓을 확인해야
        // "서버는 내려갔는데 LED 는 초록" 상태가 생기지 않는다.
        private readonly System.Windows.Forms.Timer _linkTimer =
            new System.Windows.Forms.Timer();

        private const int ConnectTimeoutMs = 5000;
        private const int MaxLogLines = 2000;
        private const int LedBlinkIntervalMs = 200;      // 초록 (연결됨)
        private const int LedBlinkRedIntervalMs = 1000;  // 빨강 (오류)
        private const int LinkCheckIntervalMs = 1000;

        public Form1()
        {
            InitializeComponent();
            _selectedFile = string.Empty;

            _ledTimer.Interval = LedBlinkIntervalMs;
            _ledTimer.Tick += LedTimer_Tick;

            _linkTimer.Interval = LinkCheckIntervalMs;
            _linkTimer.Tick += LinkTimer_Tick;
            _linkTimer.Start();

            SetLed(LedState.Idle);
            UpdateUiState();
        }

        // ------------------------------------------------------------ 연결

        private async void Btn_SOCKETConnect_Click(object sender, EventArgs e)
        {
            if (_busy)
            {
                return;
            }

            if (_conn != null && _conn.IsConnected)
            {
                await DisconnectAsync("user requested disconnect").ConfigureAwait(true);
                return;
            }

            string host = Tbox_TargetIP.Text.Trim();
            int port;

            if (host.Length == 0)
            {
                MessageBox.Show(this, "IP 주소를 입력하세요.", "Zhenyu_LoggerAnalyzer",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (!int.TryParse(Tbox_TargetPort.Text.Trim(), NumberStyles.Integer,
                              CultureInfo.InvariantCulture, out port) || port < 1 || port > 65535)
            {
                MessageBox.Show(this, "포트는 1~65535 사이의 숫자여야 합니다.", "Zhenyu_LoggerAnalyzer",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            _busy = true;
            SetLed(LedState.Connecting);
            SetStatus(string.Format("Connecting to {0}:{1} ...", host, port));
            AppendLog(string.Format("connecting to {0}:{1}", host, port));
            UpdateUiState();

            _cts = new CancellationTokenSource();

            try
            {
                // 접속과 핸드셰이크 전체가 워커에서 돌고, UI 스레드는 대기만 한다.
                _conn = await ConnectionFactory.ConnectTcpAsync(
                    host, port, "Zhenyu_LoggerAnalyzer/WinForms", ConnectTimeoutMs, _cts.Token)
                    .ConfigureAwait(true);

                // 접속 시도 중에 창이 닫혔다면 컨트롤이 이미 파괴됐다.
                // 방금 만든 연결만 반환하고 조용히 끝낸다.
                if (IsDisposed || Disposing)
                {
                    _conn.Dispose();
                    _conn = null;
                    return;
                }

                HelloAck ack = _conn.Ack;

                SetLed(LedState.Connected);
                SetStatus(string.Format(
                    "Connected to {0}  |  session #{1}  |  server sees me as {2}",
                    _conn.RemoteEndpoint, ack.SessionId, ack.ObservedPeer));

                AppendLog("connected   local=" + _conn.LocalEndpoint);
                AppendLog("HELLO_ACK   session_id=" + ack.SessionId
                          + "  max_chunk=" + ack.MaxChunk.ToString("N0", CultureInfo.InvariantCulture) + " bytes");
                AppendLog("            server sees me as " + ack.ObservedPeer);
                AppendLog("            server = " + ack.ServerVersion);
            }
            catch (OperationCanceledException)
            {
                // 창이 닫히면서 취소된 경우, 이미 파괴된 컨트롤을 만지면 안 된다.
                DropConnection();
                if (IsDisposed || Disposing) { return; }
                AppendLog("connect cancelled");
                SetStatus("Cancelled");
            }
            catch (TimeoutException ex)
            {
                DropConnection();
                if (IsDisposed || Disposing) { return; }
                AppendLog("[TIMEOUT] " + ex.Message);
                SetStatus("Connect timed out");
                SetLed(LedState.Failed);
                MessageBox.Show(this, ex.Message, "Connect failed",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch (SocketException ex)
            {
                // 서버가 안 떠 있거나 방화벽에 막힌 전형적인 경우.
                DropConnection();
                if (IsDisposed || Disposing) { return; }
                AppendLog(string.Format("[SOCKET {0}] {1}", ex.SocketErrorCode, ex.Message));
                SetStatus("Connect failed: " + ex.SocketErrorCode);
                SetLed(LedState.Failed);
                MessageBox.Show(this,
                    ex.Message + Environment.NewLine + Environment.NewLine +
                    "서버가 실행 중인지, 포트와 방화벽 설정을 확인하세요.",
                    "Connect failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch (ProtocolException ex)
            {
                DropConnection();
                if (IsDisposed || Disposing) { return; }
                AppendLog("[PROTOCOL] " + ex.Message);
                SetStatus("Protocol error");
                SetLed(LedState.Failed);
                MessageBox.Show(this, ex.Message, "Protocol error",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch (Exception ex)
            {
                DropConnection();
                if (IsDisposed || Disposing) { return; }
                AppendLog("[ERROR] " + ex.GetType().Name + ": " + ex.Message);
                SetStatus("Connect failed");
                SetLed(LedState.Failed);
                MessageBox.Show(this, ex.Message, "Connect failed",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                _busy = false;
                if (!IsDisposed && !Disposing)
                {
                    UpdateUiState();
                }
            }
        }

        private async Task DisconnectAsync(string reason)
        {
            _busy = true;
            UpdateUiState();

            AppendLog("disconnecting (" + reason + ")");

            try
            {
                if (_conn != null && _conn.IsConnected)
                {
                    using (var byeCts = new CancellationTokenSource(1000))
                    {
                        await _conn.TrySendByeAsync(byeCts.Token).ConfigureAwait(true);
                    }
                }
            }
            catch (Exception ex)
            {
                AppendLog("(ignored while disconnecting) " + ex.Message);
            }
            finally
            {
                DropConnection();
                _busy = false;
                if (!IsDisposed && !Disposing)
                {
                    SetLed(LedState.Idle);
                    SetStatus("Disconnected");
                    AppendLog("disconnected");
                    UpdateUiState();
                }
            }
        }

        /// <summary>
        /// 회선 문제로 전송이 끝났을 때: 연결을 정리하고 LED 도 끊김으로 되돌린다.
        /// 랜선이 뽑혀도 TcpClient.Connected 는 한동안 true 라 그 값은 믿지 않는다.
        /// </summary>
        private void MarkConnectionLost(string reason)
        {
            if (_conn == null)
            {
                return;
            }

            AppendLog("connection dropped (" + reason + ") - press Connect to reconnect");
            DropConnection();
            SetLed(LedState.Failed);
            UpdateUiState();
        }

        /// <summary>
        /// 연결 자원을 즉시 반환한다. 여러 번 불려도, 창이 이미 파괴된 뒤에
        /// 불려도 안전하다 (컨트롤은 살아 있을 때만 만진다).
        /// </summary>
        private void DropConnection()
        {
            if (_conn != null)
            {
                _conn.Dispose();
                _conn = null;
            }
            if (_cts != null)
            {
                _cts.Dispose();
                _cts = null;
            }

            if (IsDisposed || Disposing)
            {
                return;
            }
            SetLed(LedState.Idle);

            // 연결이 사라졌으니 상태바의 전송 세그먼트도 비운다.
            Lbl_StatusSpeed.Text = string.Empty;
            Lbl_StatusElapsed.Text = string.Empty;
        }

        // -------------------------------------------------------- 파일 선택

        private void Btn_Open_Click(object sender, EventArgs e)
        {
            if (openFileDialog1.ShowDialog(this) != DialogResult.OK)
            {
                return;
            }

            string path = openFileDialog1.FileName;

            try
            {
                var info = new FileInfo(path);
                if (!info.Exists)
                {
                    MessageBox.Show(this, "파일을 찾을 수 없습니다.", "Zhenyu_LoggerAnalyzer",
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                _selectedFile = path;
                _selectedFileSize = info.Length;

                textBox_FileLocation.Text = path;
                Lbl_FileSize.Text = string.Format(CultureInfo.InvariantCulture,
                    "{0}  ({1:N0} bytes)", HumanBytes(info.Length), info.Length);

                AppendLog("selected " + path + "  " + HumanBytes(info.Length));
            }
            catch (Exception ex)
            {
                AppendLog("[ERROR] cannot read file info: " + ex.Message);
                MessageBox.Show(this, ex.Message, "File error",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }

            UpdateUiState();
        }

        // -------------------------------------------------------- 파일 전송

        private async void Btn_Send_Click(object sender, EventArgs e)
        {
            if (_uploading || _busy)
            {
                return;
            }
            if (_conn == null || !_conn.IsConnected)
            {
                MessageBox.Show(this, "먼저 서버에 접속하세요.", "Zhenyu_LoggerAnalyzer",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (_selectedFile.Length == 0)
            {
                MessageBox.Show(this, "전송할 로그 파일을 선택하세요.", "Zhenyu_LoggerAnalyzer",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            _uploading = true;
            _uploadCts = new CancellationTokenSource();
            ResetProgress();
            UpdateUiState();

            AppendLog("upload begin  " + Path.GetFileName(_selectedFile)
                      + "  " + HumanBytes(_selectedFileSize));

            // Progress<T> 는 생성된 스레드(여기서는 UI 스레드)의
            // SynchronizationContext 로 콜백을 되돌려 준다. 덕분에
            // 워커에서 Report 를 불러도 Invoke 보일러플레이트가 필요 없다.
            var progress = new Progress<UploadProgress>(OnUploadProgress);
            var netLog = new Progress<string>(AppendLog);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            _uploadClock = sw;

            _liveRevision = -1;
            BeginLiveGrid();

            try
            {
                IServerConnection conn = _conn;
                CancellationToken token = _uploadCts.Token;
                string path = _selectedFile;

                // 과제 요구사항: 전송은 반드시 워커 스레드에서.
                // UI 스레드는 await 로 기다리기만 하므로 메시지 펌프가 계속 돈다.
                AnalyzeResult result = await Task.Run(
                    () => conn.UploadAsync(path, progress, netLog, token), token)
                    .ConfigureAwait(true);

                sw.Stop();

                double secs = sw.Elapsed.TotalSeconds;
                double mbps = secs > 0.0
                    ? (result.TotalBytes / (1024.0 * 1024.0)) / secs
                    : 0.0;

                Pbar_Analyze.Value = Pbar_Analyze.Maximum;
                Lbl_AnalyzePct.Text = "100.0 %";

                AppendLog("upload done   " + result.TotalBytes.ToString("N0", CultureInfo.InvariantCulture)
                          + " bytes in " + sw.Elapsed.TotalSeconds.ToString("F1", CultureInfo.InvariantCulture)
                          + " s  (" + mbps.ToString("F1", CultureInfo.InvariantCulture) + " MiB/s)");
                AppendLog("analysis      lines=" + result.TotalLines.ToString("N0", CultureInfo.InvariantCulture)
                          + "  accepted=" + result.AcceptedLines.ToString("N0", CultureInfo.InvariantCulture)
                          + "  rejected=" + result.RejectedLines);
                AppendLog("              spd_samples=" + result.SpdSamples.ToString("N0", CultureInfo.InvariantCulture)
                          + "  spd_average=" + result.SpdAverage);
                AppendLog("              parse=" + result.ElapsedMs + " ms"
                          + "  peakRSS=" + (result.PeakRssKb / 1024.0).ToString("F1", CultureInfo.InvariantCulture) + " MiB");
                AppendLog("result.csv    received " + result.Csv.Length.ToString("N0", CultureInfo.InvariantCulture) + " bytes");

                LoadResult(result);

                SetStatus(string.Format(CultureInfo.InvariantCulture,
                    "Done  |  {0:N0} lines ({1:N0} accepted / {2} rejected)  |  avg spd {3}  |  {4:F1} MiB/s  |  server peak RSS {5:F1} MiB",
                    result.TotalLines, result.AcceptedLines, result.RejectedLines,
                    result.SpdAverage, mbps, result.PeakRssKb / 1024.0));

                // 100% 완료 안내. 저장은 아래의 Save result.csv 버튼으로 한다.
                MessageBox.Show(this,
                    "서버로 부터 분석된 파일을 전송 받았습니다. (100%)\n"
                    + "이제 결과 파일(result.csv)을 다운 받을 수 있습니다.",
                    "Zhenyu_LoggerAnalyzer", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (OperationCanceledException)
            {
                // 중단된 전송의 진행률을 화면에 남겨두면 "얼마나 갔는지"가
                // 아니라 "멈춘 건지 진행 중인지"로 읽힌다. 0 으로 되돌린다.
                ResetProgress();
                EndLiveGrid("cancelled");
                AppendLog("upload cancelled");
                SetStatus("Upload cancelled");
            }
            catch (LocalFileException ex)
            {
                // 파일 문제일 뿐 연결은 멀쩡하다 (전송 엔진이 CANCEL 로 세션을
                // 원위치시켰다). 여기서 MarkConnectionLost 를 부르면 살아 있는
                // 연결을 끊어버리게 된다.
                ResetProgress();
                EndLiveGrid("failed");
                AppendLog("[FILE] " + ex.Message);
                SetStatus("Upload failed: local file error");
                MessageBox.Show(this, ex.Message, "Upload failed",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch (ProtocolException ex)
            {
                ResetProgress();
                EndLiveGrid("failed");
                AppendLog("[PROTOCOL] " + ex.Message);
                SetStatus("Protocol error: " + ex.Code);
                MarkConnectionLost("protocol error during upload");
                MessageBox.Show(this, ex.Message, "Upload failed",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch (Exception ex)
            {
                // 회선이 끊긴 경우. 크래시 없이 정리한다.
                ResetProgress();
                EndLiveGrid("failed");
                AppendLog("[ERROR] " + ex.GetType().Name + ": " + ex.Message);
                SetStatus("Upload failed");
                MarkConnectionLost("connection lost during upload");
                MessageBox.Show(this, ex.Message, "Upload failed",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                _uploading = false;
                if (_uploadCts != null)
                {
                    _uploadCts.Dispose();
                    _uploadCts = null;
                }

                UpdateUiState();

                // 업로드 도중 창을 닫으려 했다면 이제 실제로 닫는다.
                // finally 안에서 곧바로 Close() 를 부르지 않고 메시지 큐로
                // 넘기는 이유: 이 핸들러가 완전히 빠져나간 뒤에 닫아야
                // FormClosing 이 다시 취소되는 일이 없다.
                if (_closeRequested)
                {
                    BeginInvoke(new Action(Close));
                }
            }
        }

        // -------------------------------------------------------- 결과 표시

        /// <summary>
        /// 받은 result.csv 를 섹션으로 나눠 콤보박스에 채우고
        /// 첫 섹션을 그리드에 보여준다.
        ///
        /// 표시 순서는 과제의 두 작업이 먼저 오도록 재배열한다:
        ///   작업1 = HOURLY_MODULE_COUNTS (모듈 발생 횟수의 시간대별 그룹화)
        ///   작업2 = SUMMARY 의 spd_average (평균 속도)
        /// CSV 원본의 순서는 바꾸지 않는다 - 화면 표시 순서만 다르다.
        /// </summary>
        private void LoadResult(AnalyzeResult result)
        {
            _resultCsv = result.Csv;
            _sections = CsvDocument.Parse(_resultCsv);

            ReorderForTasks(_sections);

            Cbo_Section.BeginUpdate();
            Cbo_Section.Items.Clear();
            foreach (CsvSection s in _sections)
            {
                Cbo_Section.Items.Add(SectionLabel(s));
            }
            Cbo_Section.EndUpdate();

            Cbo_Section.Enabled = _sections.Count > 0;
            Btn_Download.Enabled = _resultCsv.Length > 0;

            if (_sections.Count > 0)
            {
                Cbo_Section.SelectedIndex = 0;   // SelectedIndexChanged 가 그리드를 채운다
            }
            else
            {
                Grid_Result.DataSource = null;
            }
        }

        /// <summary>섹션 이름 비교용. 서버가 강조를 위해 붙인 ★ 를 떼고 본다.</summary>
        private static string BareName(CsvSection s)
        {
            return s.Name.TrimStart('★');
        }

        /// <summary>과제의 작업 1/2 에 해당하는 섹션을 목록 맨 앞으로 옮긴다.</summary>
        private static void ReorderForTasks(List<CsvSection> sections)
        {
            // 뒤에서부터 꽂아야 최종 순서가 [작업1, 작업2, 나머지...] 가 된다.
            foreach (string name in new[] { "SUMMARY", "HOURLY_MODULE_COUNTS" })
            {
                int at = sections.FindIndex(s => BareName(s) == name);
                if (at > 0)
                {
                    CsvSection sec = sections[at];
                    sections.RemoveAt(at);
                    sections.Insert(0, sec);
                }
            }
        }

        /// <summary>콤보박스에 보여줄 섹션 이름. 과제 작업에 해당하면 표시한다.</summary>
        private static string SectionLabel(CsvSection s)
        {
            string prefix = string.Empty;
            if (BareName(s) == "HOURLY_MODULE_COUNTS")
            {
                prefix = "작업1 - ";
            }
            else if (BareName(s) == "SUMMARY")
            {
                prefix = "작업2 - ";
            }
            return prefix + s.Name + "  (" + s.Rows.Count + " rows)";
        }

        private void Cbo_Section_SelectedIndexChanged(object sender, EventArgs e)
        {
            int i = Cbo_Section.SelectedIndex;
            if (_sections == null || i < 0 || i >= _sections.Count)
            {
                return;
            }
            ShowSection(_sections[i]);
        }

        private void ShowSection(CsvSection section)
        {
            var table = new DataTable();

            for (int c = 0; c < section.Header.Length; c++)
            {
                string name = section.Header[c];
                if (name.Length == 0)
                {
                    name = "col" + c;
                }
                // DataTable 은 같은 이름의 열을 허용하지 않는다.
                string unique = name;
                int suffix = 2;
                while (table.Columns.Contains(unique))
                {
                    unique = name + "_" + suffix;
                    suffix++;
                }
                table.Columns.Add(unique, typeof(string));
            }

            foreach (string[] row in section.Rows)
            {
                DataRow r = table.NewRow();
                int n = Math.Min(row.Length, table.Columns.Count);
                for (int c = 0; c < n; c++)
                {
                    r[c] = row[c];
                }
                table.Rows.Add(r);
            }

            Grid_Result.DataSource = table;
        }

        private void Btn_Download_Click(object sender, EventArgs e)
        {
            if (_resultCsv.Length == 0)
            {
                MessageBox.Show(this, "저장할 결과가 없습니다. 먼저 파일을 전송하세요.",
                                "Zhenyu_LoggerAnalyzer", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            saveFileDialog1.FileName = "result.csv";
            if (saveFileDialog1.ShowDialog(this) != DialogResult.OK)
            {
                return;
            }

            try
            {
                // 서버가 만든 바이트를 그대로 보존하기 위해 BOM 없이 UTF-8 로 쓴다.
                File.WriteAllText(saveFileDialog1.FileName, _resultCsv, new UTF8Encoding(false));
                AppendLog("saved " + saveFileDialog1.FileName
                          + "  (" + _resultCsv.Length.ToString("N0", CultureInfo.InvariantCulture) + " bytes)");
                SetStatus("Saved " + saveFileDialog1.FileName);
            }
            catch (Exception ex)
            {
                AppendLog("[ERROR] save failed: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Save failed",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void Btn_Cancel_Click(object sender, EventArgs e)
        {
            if (_uploadCts == null)
            {
                return;
            }
            AppendLog("cancel requested");
            Btn_Cancel.Enabled = false;
            try { _uploadCts.Cancel(); } catch (ObjectDisposedException) { }
        }

        private void OnUploadProgress(UploadProgress p)
        {
            if (p.TotalBytes <= 0)
            {
                return;
            }

            // 진행바는 하나만 쓰고, 기준은 '서버가 분석까지 끝낸 양'이다.
            // 보낸 양(BytesSent) 기준으로 하면 소켓 버퍼 때문에 바가 먼저
            // 100% 가 되어 버려 "다 됐는데 왜 안 끝나지"가 된다.
            int donePermille = (int)(p.BytesConsumed * 1000L / p.TotalBytes);

            Pbar_Analyze.Value = Clamp(donePermille, 0, Pbar_Analyze.Maximum);
            Lbl_AnalyzePct.Text = (donePermille / 10.0).ToString("F1", CultureInfo.InvariantCulture) + " %";

            SetStatus(string.Format(CultureInfo.InvariantCulture,
                "Uploading  {0} / {1}   |   server consumed {2}   |   {3:N0} lines parsed",
                HumanBytes(p.BytesSent), HumanBytes(p.TotalBytes),
                HumanBytes(p.BytesConsumed), p.LinesParsed));

            // 상태바 우측 세그먼트: 전송 속도와 경과 시간.
            if (_uploadClock != null)
            {
                double secs = _uploadClock.Elapsed.TotalSeconds;
                Lbl_StatusSpeed.Text = secs > 0.5
                    ? (p.BytesSent / (1024.0 * 1024.0) / secs)
                          .ToString("F1", CultureInfo.InvariantCulture) + " MiB/s"
                    : string.Empty;
                Lbl_StatusElapsed.Text = _uploadClock.Elapsed.ToString("mm\\:ss");
            }

            // 서버 통계는 16MiB 마다 한 번씩만 갱신된다. 그 사이에 오는
            // 진행 보고(50ms 마다)까지 그리드를 다시 그리면 낭비이므로
            // 실제로 값이 바뀐 경우에만 갱신한다.
            if (p.StatsRevision != _liveRevision)
            {
                _liveRevision = p.StatsRevision;
                UpdateLiveGrid(p);
            }
        }

        // -------------------------------------------------- 실시간 결과 표시

        /// <summary>전송을 시작할 때 그리드를 실시간 모드로 돌린다.</summary>
        private void BeginLiveGrid()
        {
            _sections = null;
            _resultCsv = string.Empty;

            Cbo_Section.BeginUpdate();
            Cbo_Section.Items.Clear();
            Cbo_Section.Items.Add("LIVE  (analyzing...)");
            Cbo_Section.EndUpdate();
            Cbo_Section.SelectedIndex = 0;
            Cbo_Section.Enabled = false;
            Btn_Download.Enabled = false;

            _liveTable = new DataTable();
            _liveTable.Columns.Add("metric", typeof(string));
            _liveTable.Columns.Add("value", typeof(string));
            Grid_Result.DataSource = _liveTable;
        }

        /// <summary>
        /// 진행 중 분석 결과를 그리드에 반영한다. 행을 다시 만들면 스크롤이
        /// 초기화되므로, 행 개수가 같으면 값만 덮어쓴다.
        /// </summary>
        private void UpdateLiveGrid(UploadProgress p)
        {
            if (_liveTable == null)
            {
                return;
            }

            var rows = new List<string[]>();
            rows.Add(new[] { "bytes_received",
                             string.Format(CultureInfo.InvariantCulture, "{0} / {1}  ({2:F1}%)",
                                           HumanBytes(p.BytesConsumed), HumanBytes(p.TotalBytes),
                                           p.TotalBytes > 0 ? 100.0 * p.BytesConsumed / p.TotalBytes : 0.0) });
            rows.Add(new[] { "lines_parsed",  p.LinesParsed.ToString("N0", CultureInfo.InvariantCulture) });
            rows.Add(new[] { "accepted_lines", p.AcceptedLines.ToString("N0", CultureInfo.InvariantCulture) });
            rows.Add(new[] { "rejected_lines", p.RejectedLines.ToString("N0", CultureInfo.InvariantCulture) });
            rows.Add(new[] { "spd_samples",   p.SpdSamples.ToString("N0", CultureInfo.InvariantCulture) });
            rows.Add(new[] { "spd_average",   string.IsNullOrEmpty(p.SpdAverage) ? "-" : p.SpdAverage });

            if (p.ModuleNames != null && p.ModuleCounts != null)
            {
                int n = Math.Min(p.ModuleNames.Length, p.ModuleCounts.Length);
                for (int i = 0; i < n; i++)
                {
                    rows.Add(new[] { p.ModuleNames[i],
                                     p.ModuleCounts[i].ToString("N0", CultureInfo.InvariantCulture) });
                }
            }

            if (_liveTable.Rows.Count == rows.Count)
            {
                for (int i = 0; i < rows.Count; i++)
                {
                    _liveTable.Rows[i][0] = rows[i][0];
                    _liveTable.Rows[i][1] = rows[i][1];
                }
            }
            else
            {
                _liveTable.Rows.Clear();
                foreach (string[] r in rows)
                {
                    _liveTable.Rows.Add(r[0], r[1]);
                }
            }
        }

        private void ResetProgress()
        {
            Pbar_Analyze.Value = 0;
            Lbl_AnalyzePct.Text = "0.0 %";
            Lbl_StatusSpeed.Text = string.Empty;
            Lbl_StatusElapsed.Text = string.Empty;
        }

        /// <summary>
        /// 전송이 결과 없이 끝났을 때 실시간 그리드를 정리한다.
        /// 마지막으로 받은 수치는 남겨둔다 - 어디까지 처리됐는지가
        /// 취소/실패 상황에서 오히려 알고 싶은 정보다.
        /// </summary>
        private void EndLiveGrid(string reason)
        {
            if (Cbo_Section.Items.Count == 1)
            {
                Cbo_Section.Items[0] = "LIVE  (" + reason + ")";
            }
            Cbo_Section.Enabled = false;
            Btn_Download.Enabled = false;
        }

        private static int Clamp(int v, int lo, int hi)
        {
            if (v < lo) return lo;
            if (v > hi) return hi;
            return v;
        }

        // ------------------------------------------------------------ 종료

        private void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            // 전송 중에 여기서 작업이 끝나기를 기다리면 UI 가 얼어붙는다.
            // 취소 신호만 보내고 닫기를 일단 취소한 뒤, 업로드 완료 처리에서
            // 다시 Close() 를 부른다.
            if (_uploading)
            {
                e.Cancel = true;
                _closeRequested = true;
                if (_uploadCts != null)
                {
                    try { _uploadCts.Cancel(); } catch (ObjectDisposedException) { }
                }
                SetStatus("Cancelling before exit...");
                return;
            }

            if (_cts != null)
            {
                try { _cts.Cancel(); } catch (ObjectDisposedException) { }
            }
            DropConnection();

            _ledTimer.Stop();
            _ledTimer.Dispose();
            _linkTimer.Stop();
            _linkTimer.Dispose();
        }

        // -------------------------------------------------------- UI 헬퍼

        private enum LedState
        {
            Idle,          // 아직 접속한 적 없음 - 투명
            Connecting,    // 접속 시도 중 - 빨강 고정
            Connected,     // 접속됨 - 초록 200ms 점멸
            Failed,        // 접속 실패 / 오류 - 빨강 1초 점멸
        }

        /// <summary>
        /// 연결 상태 LED. 접속되면 초록 200ms, 오류면 빨강 1초 점멸 —
        /// UI 스레드가 막히면 점멸도 멈추므로, 화면 정지와 실제 상태를
        /// 눈으로 구분할 수 있다.
        /// </summary>
        private void SetLed(LedState state)
        {
            _ledState = state;
            _ledOn = true;

            if (state == LedState.Connected)
            {
                pictureBox_SocketLED.Image = Properties.Resources.LedGreen;
                _ledTimer.Interval = LedBlinkIntervalMs;
                _ledTimer.Start();
                return;
            }

            if (state == LedState.Failed)
            {
                pictureBox_SocketLED.Image = Properties.Resources.LedRed;
                _ledTimer.Interval = LedBlinkRedIntervalMs;
                _ledTimer.Start();
                return;
            }

            _ledTimer.Stop();

            switch (state)
            {
                case LedState.Connecting:
                    pictureBox_SocketLED.Image = Properties.Resources.LedRed;
                    break;
                default:
                    pictureBox_SocketLED.Image = Properties.Resources.LedTransparent;
                    break;
            }
        }

        /// <summary>
        /// 유휴 연결의 생존 확인. 전송/접속이 진행 중일 때는 그쪽 경로가
        /// 오류를 직접 감지하므로 건너뛴다.
        /// </summary>
        private void LinkTimer_Tick(object sender, EventArgs e)
        {
            if (_conn == null || _busy || _uploading)
            {
                return;
            }

            if (_conn.IsLinkDead())
            {
                SetStatus("Server closed the connection");
                MarkConnectionLost("server closed the connection");
            }
        }

        private void LedTimer_Tick(object sender, EventArgs e)
        {
            if (_ledState != LedState.Connected && _ledState != LedState.Failed)
            {
                _ledTimer.Stop();
                return;
            }

            _ledOn = !_ledOn;

            if (!_ledOn)
            {
                pictureBox_SocketLED.Image = Properties.Resources.LedTransparent;
                return;
            }
            pictureBox_SocketLED.Image = _ledState == LedState.Connected
                ? Properties.Resources.LedGreen
                : Properties.Resources.LedRed;
        }

        private void SetStatus(string text)
        {
            // 하단 StatusStrip 의 라벨. ToolStripStatusLabel 은 자체 여백이
            // 있으므로 별도 들여쓰기가 필요 없다.
            Lbl_Status.Text = text;
        }

        private void AppendLog(string text)
        {
            string line = string.Format(CultureInfo.InvariantCulture, "[{0:HH:mm:ss.fff}] {1}",
                                        DateTime.Now, text);

            // 로그가 무한히 자라지 않도록 오래된 줄을 버린다.
            if (Tbox_Log.Lines.Length >= MaxLogLines)
            {
                string[] kept = new string[MaxLogLines / 2];
                Array.Copy(Tbox_Log.Lines, Tbox_Log.Lines.Length - kept.Length, kept, 0, kept.Length);
                Tbox_Log.Lines = kept;
            }

            Tbox_Log.AppendText(line + Environment.NewLine);
        }

        private void UpdateUiState()
        {
            bool connected = _conn != null && _conn.IsConnected;
            bool hasFile = _selectedFile.Length > 0;
            bool idle = !_busy && !_uploading;

            Btn_SOCKETConnect.Text = connected ? "Disconnect" : "Connect";
            Btn_SOCKETConnect.Enabled = idle;

            Tbox_TargetIP.Enabled = !connected && idle;
            Tbox_TargetPort.Enabled = !connected && idle;

            Btn_Open.Enabled = idle;
            Btn_Send.Enabled = connected && hasFile && idle;
            Btn_Cancel.Enabled = _uploading;
        }

        private static string HumanBytes(long n)
        {
            const double Ki = 1024.0;
            double v = n;

            if (v >= Ki * Ki * Ki)
            {
                return (v / (Ki * Ki * Ki)).ToString("F2", CultureInfo.InvariantCulture) + " GiB";
            }
            if (v >= Ki * Ki)
            {
                return (v / (Ki * Ki)).ToString("F1", CultureInfo.InvariantCulture) + " MiB";
            }
            if (v >= Ki)
            {
                return (v / Ki).ToString("F1", CultureInfo.InvariantCulture) + " KiB";
            }
            return n.ToString(CultureInfo.InvariantCulture) + " B";
        }
    }
}
