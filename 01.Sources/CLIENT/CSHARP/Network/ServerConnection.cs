// 서버와의 TCP 연결 하나를 담당한다 (IServerConnection 구현).
//
// - 자원 해제: Dispose 가 소켓/스트림을 즉시 닫는다. C# 판 RAII.
// - UI 비차단: 모든 I/O 는 async — 메시지 펌프가 멈추지 않는다.
// - 취소: .NET Framework 의 NetworkStream.ReadAsync 는 토큰을 사실상
//   무시하므로, 취소를 진행 중인 읽기와 경주시켜 CANCEL 프레임을 보낸다.
//   서버의 응답(ERROR 또는 남은 결과 프레임)이 읽기를 자연스럽게 풀고,
//   한도 안에 응답이 없을 때만 소켓을 닫아 강제로 깨운다.

using System;
using System.Globalization;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Individual_Assignment01_UI.Network
{
    /// <summary>프로토콜 규약 위반 또는 서버가 보낸 ERROR 프레임.</summary>
    internal sealed class ProtocolException : Exception
    {
        public ProtocolError Code { get; private set; }

        public ProtocolException(string message)
            : base(message)
        {
            Code = ProtocolError.None;
        }

        public ProtocolException(ProtocolError code, string message)
            : base(message)
        {
            Code = code;
        }
    }

    /// <summary>
    /// 서버/회선과 무관한 로컬 파일 문제 (없는 파일, 열기 실패, 디스크 읽기
    /// 오류, 업로드 도중 크기 변경). UI 는 이 예외를 받으면 연결을 끊지
    /// 않아야 한다 - 연결은 멀쩡하고, 파일만 문제이기 때문이다.
    /// </summary>
    internal sealed class LocalFileException : Exception
    {
        public LocalFileException(string message)
            : base(message)
        {
        }

        public LocalFileException(string message, Exception inner)
            : base(message, inner)
        {
        }
    }

    internal sealed class ServerConnection : IServerConnection
    {
        // 취소 후 서버 응답을 기다리는 한도. 서버가 소켓 버퍼에 남은 청크를
        // 소화하는 시간이 필요하므로 넉넉히 잡는다.
        private const int CancelAckTimeoutMs = 5000;

        // 청크 하나를 쓰는 데 이보다 오래 걸리면 회선이 끊긴 것으로 본다.
        // 느린 링크(2 MiB/s)에서도 1 MiB 청크는 1초면 나가므로 넉넉한 값이다.
        private const int WriteStallTimeoutMs = 20000;

        // 동기 Read (업로드 중 ACK 드레인) 의 한도. DataAvailable 은 "1바이트
        // 이상"만 보장하므로, 프레임 뒷부분이 오지 않으면 동기 Read 가
        // 무한정 멈출 수 있다. UPLOAD_ACK 은 100바이트 남짓이라 5초는
        // 사실상 회선 사망 판정이다.
        private const int SyncReadTimeoutMs = 5000;

        private TcpClient _client;
        private NetworkStream _stream;
        private bool _disposed;

        public HelloAck Ack { get; private set; }
        public string LocalEndpoint { get; private set; }
        public string RemoteEndpoint { get; private set; }

        public bool IsConnected
        {
            get { return !_disposed && _client != null && _client.Client != null && _client.Connected; }
        }

        private ServerConnection()
        {
            LocalEndpoint = string.Empty;
            RemoteEndpoint = string.Empty;
        }

        /// <summary>
        /// 접속하고 HELLO / HELLO_ACK 핸드셰이크까지 끝낸 연결을 돌려준다.
        /// 실패하면 예외를 던지고, 그 과정에서 만든 소켓은 전부 닫는다.
        /// </summary>
        public static async Task<ServerConnection> ConnectAsync(
            string host, int port, string clientName, int timeoutMs, CancellationToken ct)
        {
            var conn = new ServerConnection();
            try
            {
                conn._client = new TcpClient();
                conn._client.NoDelay = true;

                // TcpClient.ConnectAsync 에는 타임아웃도 CancellationToken 도 없다.
                // 그래서 Task.WhenAny 로 경주시키는데, '진 쪽'을 반드시 정리해야 한다.
                using (var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(ct))
                {
                    Task connectTask = conn._client.ConnectAsync(host, port);
                    Task timeoutTask = Task.Delay(timeoutMs, timeoutCts.Token);

                    Task finished = await Task.WhenAny(connectTask, timeoutTask)
                                              .ConfigureAwait(false);

                    if (finished == connectTask)
                    {
                        // 타이머를 즉시 해제한다. 그러지 않으면 접속이 성공한 뒤에도
                        // timeoutMs 동안 살아 있고, Connect 를 반복하면 쌓인다.
                        timeoutCts.Cancel();

                        // 접속 자체가 실패했으면 여기서 SocketException 이 다시 던져진다.
                        await connectTask.ConfigureAwait(false);
                    }
                    else
                    {
                        // 타임아웃(또는 취소)이 이겼다. 접속 진행 중인 소켓을
                        // 여기서 바로 Close() 하면 프레임워크 내부(EndConnect)에서
                        // 예외가 터지므로, 떼어냈다가 시도가 끝난 뒤 닫는다.
                        TcpClient abandoned = conn._client;
                        conn._client = null;
                        CloseWhenSettled(abandoned, connectTask);

                        ct.ThrowIfCancellationRequested();
                        throw new TimeoutException(string.Format(
                            "connect to {0}:{1} timed out after {2} ms", host, port, timeoutMs));
                    }
                }

                conn._stream = conn._client.GetStream();

                // 동기 Read (ReadExactlyBlocking) 가 반쪽 프레임에서 무한정
                // 멈추지 않도록 한도를 둔다. 비동기 ReadAsync 는 이 값의
                // 영향을 받지 않는다.
                conn._stream.ReadTimeout = SyncReadTimeoutMs;

                conn.LocalEndpoint = conn._client.Client.LocalEndPoint.ToString();
                conn.RemoteEndpoint = conn._client.Client.RemoteEndPoint.ToString();

                conn.Ack = await conn.HandshakeAsync(clientName, ct).ConfigureAwait(false);
                return conn;
            }
            catch
            {
                conn.Dispose();
                throw;
            }
        }

        /// <summary>
        /// 포기한 접속 시도의 뒷정리. 시도가 끝난 '뒤'에 소켓을 닫고
        /// (도중에 닫으면 프레임워크 내부 예외), 실패한 Task 의 Exception 을
        /// 읽어 '관측되지 않은 예외'도 함께 없앤다. 버려진 소켓이 OS 타임아웃
        /// (최대 20초 남짓)까지 잠시 남는 것이 유일한 대가다.
        /// </summary>
        private static void CloseWhenSettled(TcpClient client, Task connectTask)
        {
            if (client == null)
            {
                return;
            }

            if (connectTask == null)
            {
                try { client.Close(); } catch (Exception) { }
                return;
            }

            connectTask.ContinueWith(
                t =>
                {
                    // .Exception 을 읽는 행위 자체가 '관측'이다.
                    AggregateException observed = t.Exception;
                    GC.KeepAlive(observed);

                    // 이제 시도가 끝났으므로 닫아도 안전하다.
                    try { client.Close(); } catch (Exception) { }
                },
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }

        private async Task<HelloAck> HandshakeAsync(string clientName, CancellationToken ct)
        {
            var w = new PayloadWriter();
            w.U16(Wire.ClientProtocolVersion);
            w.Str16(clientName);
            await SendFrameAsync(MsgType.Hello, w.ToArray(), ct).ConfigureAwait(false);

            Frame f = await ReadFrameAsync(ct).ConfigureAwait(false);

            if (f.Type == MsgType.Error)
            {
                ThrowServerError(f);
            }

            if (f.Type != MsgType.HelloAck)
            {
                throw new ProtocolException(
                    "expected HELLO_ACK but received " + Wire.Name(f.Type));
            }

            var r = new PayloadReader(f.Payload);
            var ack = new HelloAck();
            uint sessionId, maxChunk;
            string observed, serverVersion;

            if (!r.U32(out sessionId) || !r.U32(out maxChunk) ||
                !r.Str16(out observed) || !r.Str16(out serverVersion))
            {
                throw new ProtocolException("malformed HELLO_ACK payload");
            }

            ack.SessionId = sessionId;
            ack.MaxChunk = maxChunk;
            ack.ObservedPeer = observed;
            ack.ServerVersion = serverVersion;
            return ack;
        }

        /// <summary>서버가 보낸 ERROR 프레임을 예외로 바꾼다.</summary>
        public static void ThrowServerError(Frame f)
        {
            var r = new PayloadReader(f.Payload);
            ushort code;
            string message;
            if (!r.U16(out code) || !r.Str16(out message))
            {
                throw new ProtocolException("malformed ERROR payload");
            }
            throw new ProtocolException((ProtocolError)code,
                string.Format("server error {0}: {1}", code, message));
        }

        public async Task SendFrameAsync(MsgType type, byte[] payload, CancellationToken ct)
        {
            EnsureUsable();
            byte[] body = payload ?? new byte[0];

            byte[] head = Wire.EncodeHeader(type, body.Length);
            await _stream.WriteAsync(head, 0, head.Length, ct).ConfigureAwait(false);
            if (body.Length > 0)
            {
                await _stream.WriteAsync(body, 0, body.Length, ct).ConfigureAwait(false);
            }
            await _stream.FlushAsync(ct).ConfigureAwait(false);
        }

        public async Task<Frame> ReadFrameAsync(CancellationToken ct)
        {
            EnsureUsable();

            var head = new byte[Wire.HeaderSize];
            await ReadExactlyAsync(head, 0, head.Length, ct).ConfigureAwait(false);

            uint magic = Wire.LoadBe32(head, 0);
            if (magic != Wire.Magic)
            {
                throw new ProtocolException(
                    string.Format("bad magic from server: 0x{0:X8}", magic));
            }

            byte version = head[4];
            if (version != Wire.Version)
            {
                throw new ProtocolException(
                    string.Format("unsupported protocol version from server: {0}", version));
            }

            var frame = new Frame();
            frame.Type = (MsgType)head[5];
            frame.Flags = Wire.LoadBe16(head, 6);

            ulong len = Wire.LoadBe64(head, 8);
            // 조작되거나 깨진 길이 하나로 거대한 배열을 잡지 않도록 막는다.
            if (len > (ulong)Wire.MaxPayload)
            {
                throw new ProtocolException(
                    string.Format("payload too large from server: {0} bytes", len));
            }

            if (len > 0)
            {
                frame.Payload = new byte[(int)len];
                await ReadExactlyAsync(frame.Payload, 0, frame.Payload.Length, ct)
                          .ConfigureAwait(false);
            }

            return frame;
        }

        /// <summary>
        /// .NET Framework 4.7.2 에는 Stream.ReadExactly 가 없다.
        /// TCP 는 스트림이라 한 번의 Read 가 요청한 만큼을 준다는 보장이 없으므로
        /// 직접 루프를 돌아야 한다. 이걸 빼먹으면 대용량 전송에서 반드시 깨진다.
        /// </summary>
        private async Task ReadExactlyAsync(byte[] buffer, int offset, int count,
                                            CancellationToken ct)
        {
            int got = 0;
            while (got < count)
            {
                ct.ThrowIfCancellationRequested();

                int n = await _stream.ReadAsync(buffer, offset + got, count - got, ct)
                                     .ConfigureAwait(false);
                if (n == 0)
                {
                    // 상대가 연결을 닫았다.
                    throw new EndOfStreamException(
                        string.Format("connection closed by server after {0} of {1} bytes",
                                      got, count));
                }
                got += n;
            }
        }

        // ------------------------------------------------------------ 업로드

        /// <summary>
        /// 파일을 서버로 스트리밍한다. 고정 버퍼 하나로 읽는 즉시 흘려보내므로
        /// 파일 크기와 무관하게 일정한 메모리만 쓴다. 워커 스레드에서 호출할 것
        /// (progress 는 Progress&lt;T&gt; 가 UI 스레드로 자동 마샬링한다).
        /// </summary>
        public async Task<AnalyzeResult> UploadAsync(
            string path, IProgress<UploadProgress> progress, IProgress<string> log,
            CancellationToken ct)
        {
            EnsureUsable();

            var info = new FileInfo(path);
            if (!info.Exists)
            {
                // 소켓에는 아직 아무것도 보내지 않았다. 연결은 멀쩡하므로
                // UI 가 끊어버리지 않도록 로컬 파일 예외로 구분해 던진다.
                throw new LocalFileException("file not found: " + path);
            }

            long total = info.Length;
            string name = Path.GetFileName(path);

            // 1) UPLOAD_BEGIN
            var begin = new PayloadWriter();
            begin.U64((ulong)total);
            begin.Str16(name);
            await SendFrameAsync(MsgType.UploadBegin, begin.ToArray(), ct).ConfigureAwait(false);
            Log(log, "-> UPLOAD_BEGIN      {0}  {1}", name, Wire.HumanBytes(total));

            Frame ack = await ReadFrameAsync(ct).ConfigureAwait(false);
            if (ack.Type == MsgType.Error)
            {
                ThrowServerError(ack);
            }
            if (ack.Type != MsgType.UploadBeginAck)
            {
                throw new ProtocolException(
                    "expected UPLOAD_BEGIN_ACK but received " + Wire.Name(ack.Type));
            }

            var ackReader = new PayloadReader(ack.Payload);
            uint uploadId, chunkSize;
            if (!ackReader.U32(out uploadId) || !ackReader.U32(out chunkSize))
            {
                throw new ProtocolException("malformed UPLOAD_BEGIN_ACK payload");
            }
            if (chunkSize == 0 || chunkSize > 4 * 1024 * 1024)
            {
                chunkSize = 1024 * 1024;
            }

            // 모듈 이름은 여기서 한 번만 받는다. 이후 UPLOAD_ACK 에는 카운트만 온다.
            string[] moduleNames = ReadModuleNames(ackReader);

            Log(log, "<- UPLOAD_BEGIN_ACK  upload_id={0}  chunk={1}  modules={2}",
                uploadId, Wire.HumanBytes(chunkSize), moduleNames.Length);

            // 2) UPLOAD_CHUNK 반복
            var state = new UploadProgress
            {
                TotalBytes = total,
                ModuleNames = moduleNames,
                ModuleCounts = new long[moduleNames.Length],
            };

            var buffer = new byte[chunkSize];      // 전송 내내 재사용하는 유일한 버퍼
            var header = new byte[Wire.HeaderSize];
            var clock = System.Diagnostics.Stopwatch.StartNew();
            long lastReportMs = -1000;

            // 로그는 UI 갱신보다 훨씬 성기게 남긴다. 청크마다 찍으면
            // 500MB 전송에 483줄이 쌓여서 읽을 수 없게 된다.
            const long LogIntervalBytes = 32L * 1024 * 1024;
            long lastLogBytes = 0;
            int chunksSinceLog = 0;

            FileStream file;
            try
            {
                file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read,
                                      (int)chunkSize, useAsync: true);
            }
            catch (Exception ex)
            {
                // 파일을 열지 못했다. 서버는 이미 UPLOAD_BEGIN 을 받고 청크를
                // 기다리는 중이므로, CANCEL 로 세션을 원위치시켜 연결을 살려
                // 둔 뒤 로컬 파일 오류로 보고한다.
                await CancelAndDrainAsync(null, log).ConfigureAwait(false);
                throw new LocalFileException("cannot open file: " + ex.Message, ex);
            }

            using (file)
            {
                while (true)
                {
                    // 취소는 어떤 I/O 보다 먼저, 이 지점에서만 판단한다.
                    // 아래 I/O 에 토큰을 넘기면 CANCEL 전송 코드에 도달하기 전에
                    // 예외로 빠져나가, 서버가 취소를 통보받지 못한다.
                    if (ct.IsCancellationRequested)
                    {
                        await CancelAndDrainAsync(null, log).ConfigureAwait(false);
                        ct.ThrowIfCancellationRequested();
                    }

                    // 선언한 총량까지만 읽는다. 업로드 도중 파일이 자라도
                    // 서버가 "선언보다 많이 왔다"(SizeMismatch) 로 연결을
                    // 끊는 일이 없다.
                    long remaining = total - state.BytesSent;
                    if (remaining == 0)
                    {
                        break;
                    }
                    int want = (int)Math.Min(buffer.Length, remaining);

                    int n;
                    try
                    {
                        n = await file.ReadAsync(buffer, 0, want, CancellationToken.None)
                                      .ConfigureAwait(false);
                    }
                    catch (Exception ex)
                    {
                        // 디스크 읽기 오류다. 회선 문제로 위장되지 않도록
                        // CANCEL 로 세션을 정리하고 로컬 오류로 보고한다.
                        await CancelAndDrainAsync(null, log).ConfigureAwait(false);
                        throw new LocalFileException("local file read error: " + ex.Message, ex);
                    }
                    if (n <= 0)
                    {
                        break;
                    }

                    Wire.StoreBe32(header, 0, Wire.Magic);
                    header[4] = Wire.Version;
                    header[5] = (byte)MsgType.UploadChunk;
                    Wire.StoreBe16(header, 6, 0);
                    Wire.StoreBe64(header, 8, (ulong)n);

                    await WriteWithStallCheckAsync(header, header.Length).ConfigureAwait(false);
                    await WriteWithStallCheckAsync(buffer, n).ConfigureAwait(false);

                    state.BytesSent += n;
                    chunksSinceLog++;

                    // 서버가 보낸 UPLOAD_ACK 을 논블로킹으로 걷어낸다.
                    // 읽지 않고 두면 서버의 송신 버퍼가 차서 교착으로 갈 수 있다.
                    DrainServerMessages(state, log);

                    if (state.BytesSent - lastLogBytes >= LogIntervalBytes)
                    {
                        double secs = clock.Elapsed.TotalSeconds;
                        double mibps = secs > 0.0
                            ? (state.BytesSent / (1024.0 * 1024.0)) / secs : 0.0;
                        Log(log, "-> UPLOAD_CHUNK x{0,-4} {1} / {2}  ({3:F1}%)  {4:F1} MiB/s",
                            chunksSinceLog, Wire.HumanBytes(state.BytesSent),
                            Wire.HumanBytes(total), Percent(state.BytesSent, total), mibps);
                        lastLogBytes = state.BytesSent;
                        chunksSinceLog = 0;
                    }

                    // UI 갱신은 50ms 로 제한한다. 청크마다 갱신하면 메시지
                    // 펌프가 익사해서 오히려 화면이 멈춘다.
                    if (progress != null && clock.ElapsedMilliseconds - lastReportMs >= 50)
                    {
                        lastReportMs = clock.ElapsedMilliseconds;
                        progress.Report(state.Snapshot());
                    }
                }

                await _stream.FlushAsync(CancellationToken.None).ConfigureAwait(false);
            }

            if (progress != null)
            {
                progress.Report(state.Snapshot());
            }

            // 2.5) 로컬 파일 이상 검사. 선언한 총량을 채우지 못하고 루프가
            // 끝났다면 업로드 도중 파일이 줄어든 것이다. 이대로 UPLOAD_END
            // 를 보내면 서버가 SizeMismatch 로 연결을 끊어 서버 오류처럼
            // 보이므로, 우리가 먼저 CANCEL 로 정리하고 로컬 오류로 알린다.
            if (state.BytesSent != total)
            {
                await CancelAndDrainAsync(null, log).ConfigureAwait(false);
                throw new LocalFileException(string.Format(CultureInfo.InvariantCulture,
                    "file changed during upload: declared {0} bytes but could only read {1}",
                    total, state.BytesSent));
            }

            // 3) UPLOAD_END
            var end = new PayloadWriter();
            end.U64((ulong)state.BytesSent);
            await SendFrameAsync(MsgType.UploadEnd, end.ToArray(), ct).ConfigureAwait(false);
            Log(log, "-> UPLOAD_END        {0} sent, waiting for the analysis",
                Wire.HumanBytes(state.BytesSent));

            // 4) ANALYZE_DONE 이 올 때까지 남은 프레임을 소화한다.
            // 이 단계는 읽기만 하므로, 취소가 눌리면 읽기와 경주시켜
            // CANCEL 을 보내야 한다 (ReadFrameOrCancelAsync).
            while (true)
            {
                Frame f = await ReadFrameOrCancelAsync(log, ct).ConfigureAwait(false);

                if (f.Type == MsgType.UploadAck)
                {
                    ApplyUploadAck(f, state, log);
                    if (progress != null)
                    {
                        progress.Report(state.Snapshot());
                    }
                    continue;
                }

                if (f.Type == MsgType.Error)
                {
                    ThrowServerError(f);
                }

                if (f.Type == MsgType.AnalyzeDone)
                {
                    AnalyzeResult result = ParseAnalyzeDone(f);

                    Log(log, "<- ANALYZE_DONE      lines={0:N0}  accepted={1:N0}  rejected={2}  "
                             + "spd_n={3:N0}  spd_avg={4}",
                        result.TotalLines, result.AcceptedLines, result.RejectedLines,
                        result.SpdSamples, result.SpdAverage);
                    Log(log, "                     parse={0} ms  server peak RSS={1:F1} MiB",
                        result.ElapsedMs, result.PeakRssKb / 1024.0);

                    state.BytesConsumed = result.TotalBytes;
                    state.LinesParsed = result.TotalLines;
                    state.AcceptedLines = result.AcceptedLines;
                    state.RejectedLines = result.RejectedLines;
                    state.SpdSamples = result.SpdSamples;
                    state.SpdAverage = result.SpdAverage;
                    state.StatsRevision++;
                    if (progress != null)
                    {
                        progress.Report(state.Snapshot());
                    }

                    // 서버는 분석이 끝나면 result.csv 를 곧바로 밀어 보낸다.
                    // 클라이언트가 따로 요청하지 않는다.
                    result.Csv = await ReceiveResultAsync(result.CsvSize, log, ct)
                                           .ConfigureAwait(false);
                    return result;
                }

                throw new ProtocolException(
                    "unexpected message while waiting for ANALYZE_DONE: " + Wire.Name(f.Type));
            }
        }

        private static AnalyzeResult ParseAnalyzeDone(Frame f)
        {
            var r = new PayloadReader(f.Payload);
            var result = new AnalyzeResult();

            ulong bytes, lines, accepted, rejected, oversize, spdN, elapsed, rss, csvSize;
            string avg;

            if (!r.U64(out bytes) || !r.U64(out lines) || !r.U64(out accepted) ||
                !r.U64(out rejected) || !r.U64(out oversize) || !r.U64(out spdN) ||
                !r.Str16(out avg) || !r.U64(out elapsed) || !r.U64(out rss) ||
                !r.U64(out csvSize))
            {
                throw new ProtocolException("malformed ANALYZE_DONE payload");
            }

            result.TotalBytes = (long)bytes;
            result.TotalLines = (long)lines;
            result.AcceptedLines = (long)accepted;
            result.RejectedLines = (long)rejected;
            result.OversizeLines = (long)oversize;
            result.SpdSamples = (long)spdN;
            result.SpdAverage = avg;
            result.ElapsedMs = (long)elapsed;
            result.PeakRssKb = (long)rss;
            result.CsvSize = (long)csvSize;
            return result;
        }

        /// <summary>RESULT_BEGIN / RESULT_CHUNK* / RESULT_END 를 받아 CSV 를 조립한다.</summary>
        private async Task<string> ReceiveResultAsync(long expectedSize, IProgress<string> log,
                                                      CancellationToken ct)
        {
            // 조작된 크기로 거대한 버퍼를 잡지 않도록 상한을 둔다.
            const long MaxCsv = 64L * 1024 * 1024;
            if (expectedSize < 0 || expectedSize > MaxCsv)
            {
                throw new ProtocolException(
                    "result.csv size is out of range: " + expectedSize);
            }

            Frame begin = await ReadFrameOrCancelAsync(log, ct).ConfigureAwait(false);
            if (begin.Type == MsgType.Error)
            {
                ThrowServerError(begin);
            }
            if (begin.Type != MsgType.ResultBegin)
            {
                throw new ProtocolException(
                    "expected RESULT_BEGIN but received " + Wire.Name(begin.Type));
            }

            var beginReader = new PayloadReader(begin.Payload);
            ulong declared;
            if (!beginReader.U64(out declared))
            {
                throw new ProtocolException("malformed RESULT_BEGIN payload");
            }
            if ((long)declared > MaxCsv)
            {
                throw new ProtocolException("result.csv too large: " + declared);
            }

            Log(log, "<- RESULT_BEGIN      result.csv  {0}", Wire.HumanBytes((long)declared));

            using (var buffer = new MemoryStream((int)Math.Min(declared, 1024 * 1024)))
            {
                while (true)
                {
                    Frame f = await ReadFrameOrCancelAsync(log, ct).ConfigureAwait(false);

                    if (f.Type == MsgType.ResultChunk)
                    {
                        buffer.Write(f.Payload, 0, f.Payload.Length);
                        continue;
                    }
                    if (f.Type == MsgType.ResultEnd)
                    {
                        break;
                    }
                    if (f.Type == MsgType.Error)
                    {
                        ThrowServerError(f);
                    }
                    throw new ProtocolException(
                        "unexpected message while receiving result.csv: " + Wire.Name(f.Type));
                }

                if (buffer.Length != (long)declared)
                {
                    throw new ProtocolException(string.Format(
                        "result.csv size mismatch: declared {0} but received {1}",
                        declared, buffer.Length));
                }

                Log(log, "<- RESULT_END        received {0:N0} bytes", buffer.Length);
                return Encoding.UTF8.GetString(buffer.GetBuffer(), 0, (int)buffer.Length);
            }
        }

        /// <summary>
        /// 지금 당장 읽을 수 있는 서버 메시지만 처리한다. 블로킹하지 않는다.
        /// </summary>
        private void DrainServerMessages(UploadProgress state, IProgress<string> log)
        {
            while (_stream != null && _stream.DataAvailable)
            {
                // 읽기 실패(서버가 닫음, 타임아웃, 리셋)는 삼키지 않고
                // 그대로 던진다. 여기서 삼키면 실패가 다음 쓰기까지 숨겨져
                // 최대 한 청크 늦게, 엉뚱한 오류로 보고된다.
                Frame f = ReadFrameBlocking();

                if (f.Type == MsgType.UploadAck)
                {
                    ApplyUploadAck(f, state, log);
                }
                else if (f.Type == MsgType.Error)
                {
                    ThrowServerError(f);
                }
                // 그 밖의 메시지는 업로드 중에 올 이유가 없으므로 무시한다.
            }
        }

        /// <summary>
        /// 쓰기가 제한 시간을 넘기면 회선이 죽은 것으로 본다. 랜선이 뽑히면
        /// RST 없이 WriteAsync 만 침묵하므로, TCP 재전송 소진(수 분)을 기다리지
        /// 않고 전송 중이라는 문맥으로 빨리 판단한다. 버린 작업의 예외는 관측해 둔다.
        /// </summary>
        private async Task WriteWithStallCheckAsync(byte[] data, int count)
        {
            Task write = _stream.WriteAsync(data, 0, count, CancellationToken.None);

            using (var cts = new CancellationTokenSource())
            {
                Task delay = Task.Delay(WriteStallTimeoutMs, cts.Token);
                Task finished = await Task.WhenAny(write, delay).ConfigureAwait(false);

                if (finished == write)
                {
                    cts.Cancel();                                  // 타이머 해제
                    await write.ConfigureAwait(false);             // 실패했다면 여기서 던진다
                    return;
                }
            }

            ObserveAndDiscard(write);
            throw new IOException(string.Format(CultureInfo.InvariantCulture,
                "network write stalled for {0} ms - the connection appears to be down",
                WriteStallTimeoutMs));
        }

        /// <summary>버리는 Task 의 예외를 관측만 해서 삼킨다.</summary>
        private static void ObserveAndDiscard(Task task)
        {
            if (task == null)
            {
                return;
            }
            task.ContinueWith(
                t => { AggregateException observed = t.Exception; GC.KeepAlive(observed); },
                CancellationToken.None,
                TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }

        /// <summary>UPLOAD_BEGIN_ACK 뒤에 붙어 오는 모듈 이름 목록.</summary>
        private static string[] ReadModuleNames(PayloadReader r)
        {
            byte count;
            if (!r.U8(out count) || count == 0 || count > 64)
            {
                return new string[0];
            }

            var names = new string[count];
            for (int i = 0; i < count; i++)
            {
                string s;
                if (!r.Str16(out s))
                {
                    return new string[0];
                }
                names[i] = s;
            }
            return names;
        }

        private static double Percent(long part, long whole)
        {
            return whole > 0 ? (100.0 * part / whole) : 0.0;
        }

        private static void Log(IProgress<string> log, string format, params object[] args)
        {
            if (log != null)
            {
                log.Report(string.Format(CultureInfo.InvariantCulture, format, args));
            }
        }

        /// <summary>
        /// DataAvailable 로 이미 데이터가 있다는 걸 확인한 뒤에만 쓴다.
        /// 프레임 뒷부분이 아직 안 왔을 수 있어 짧게 블로킹될 수는 있지만,
        /// UPLOAD_ACK 은 32바이트라 사실상 즉시 완료된다.
        /// </summary>
        private Frame ReadFrameBlocking()
        {
            var head = new byte[Wire.HeaderSize];
            ReadExactlyBlocking(head, 0, head.Length);

            if (Wire.LoadBe32(head, 0) != Wire.Magic)
            {
                throw new ProtocolException("bad magic from server during upload");
            }
            // 비동기 경로(ReadFrameAsync)와 같은 검증을 한다. 같은 프레임을
            // 해석하는 두 경로의 검증이 달라지면 안 된다.
            if (head[4] != Wire.Version)
            {
                throw new ProtocolException(string.Format(
                    "unsupported protocol version from server: {0}", head[4]));
            }

            var frame = new Frame();
            frame.Type = (MsgType)head[5];
            frame.Flags = Wire.LoadBe16(head, 6);

            ulong len = Wire.LoadBe64(head, 8);
            if (len > (ulong)Wire.MaxPayload)
            {
                throw new ProtocolException("payload too large from server during upload");
            }
            if (len > 0)
            {
                frame.Payload = new byte[(int)len];
                ReadExactlyBlocking(frame.Payload, 0, frame.Payload.Length);
            }
            return frame;
        }

        private void ReadExactlyBlocking(byte[] buffer, int offset, int count)
        {
            int got = 0;
            while (got < count)
            {
                int n = _stream.Read(buffer, offset + got, count - got);
                if (n == 0)
                {
                    throw new EndOfStreamException("connection closed by server");
                }
                got += n;
            }
        }

        /// <summary>
        /// 서버가 16MiB 마다 보내는 진행 상황. 소비한 바이트뿐 아니라
        /// 그 시점까지의 분석 결과(수용/거부/모듈별 카운트)도 함께 온다.
        /// 덕분에 전송이 끝나기 전에도 화면에 결과를 띄울 수 있다.
        /// </summary>
        private static void ApplyUploadAck(Frame f, UploadProgress state, IProgress<string> log)
        {
            var r = new PayloadReader(f.Payload);

            ulong consumed, lines;
            if (!r.U64(out consumed) || !r.U64(out lines))
            {
                return;
            }
            state.BytesConsumed = (long)consumed;
            state.LinesParsed = (long)lines;

            // 아래 필드는 서버 버전에 따라 없을 수도 있다. 읽히는 만큼만 반영한다.
            ulong accepted, rejected, spdN;
            string avg;
            if (r.U64(out accepted)) { state.AcceptedLines = (long)accepted; }
            if (r.U64(out rejected)) { state.RejectedLines = (long)rejected; }
            if (r.U64(out spdN)) { state.SpdSamples = (long)spdN; }
            if (r.Str16(out avg)) { state.SpdAverage = avg; }

            byte moduleCount;
            if (r.U8(out moduleCount) && moduleCount > 0 && moduleCount <= 64)
            {
                if (state.ModuleCounts == null || state.ModuleCounts.Length != moduleCount)
                {
                    state.ModuleCounts = new long[moduleCount];
                }
                for (int i = 0; i < moduleCount; i++)
                {
                    ulong c;
                    if (!r.U64(out c)) { break; }
                    state.ModuleCounts[i] = (long)c;
                }
            }

            state.StatsRevision++;

            Log(log, "<- UPLOAD_ACK        server consumed {0} ({1:F1}%)  "
                     + "lines={2:N0}  accepted={3:N0}  rejected={4}",
                Wire.HumanBytes(state.BytesConsumed),
                Percent(state.BytesConsumed, state.TotalBytes),
                state.LinesParsed, state.AcceptedLines, state.RejectedLines);
        }

        /// <summary>
        /// 취소가 동작하는 프레임 읽기. NetworkStream.ReadAsync 는 .NET
        /// Framework 에서 취소 토큰을 사실상 무시하므로, 토큰만 넘겨서는
        /// 서버가 다음 프레임을 보낼 때까지 취소가 먹지 않는다 (분석 대기 /
        /// 결과 수신 중 취소가 무시되고, 종료 시 창이 닫히지 않는 원인).
        ///
        /// 대신 진행 중인 읽기와 취소 신호를 경주시킨다. 취소가 먼저 오면
        /// CANCEL 을 보내고 서버 응답으로 읽기를 풀어낸 뒤 (CancelAndDrainAsync)
        /// OperationCanceledException 으로 빠져나간다.
        /// </summary>
        private async Task<Frame> ReadFrameOrCancelAsync(IProgress<string> log,
                                                         CancellationToken ct)
        {
            Task<Frame> read = ReadFrameAsync(CancellationToken.None);
            if (!ct.CanBeCanceled)
            {
                return await read.ConfigureAwait(false);
            }

            var cancelled = new TaskCompletionSource<bool>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            using (ct.Register(() => cancelled.TrySetResult(true)))
            {
                Task first = await Task.WhenAny(read, cancelled.Task).ConfigureAwait(false);
                if (first == read)
                {
                    return await read.ConfigureAwait(false);
                }
            }

            await CancelAndDrainAsync(read, log).ConfigureAwait(false);
            throw new OperationCanceledException(ct);
        }

        /// <summary>
        /// CANCEL 을 보낸 뒤, 서버가 프로토콜을 정리했다는 표시가 올 때까지
        /// 밀린 프레임을 소화한다. 표시는 두 가지다:
        ///   - ERROR: 서버가 업로드를 중단 처리했다 (세션은 Ready 로 복귀)
        ///   - RESULT_END: 취소가 늦어 서버는 이미 분석을 끝내고 결과까지
        ///     내보냈다. 여기까지 소화하면 연결은 다시 유휴 상태다.
        /// 보내자마자 닫으면 버퍼에 남은 청크 때문에 서버가 CANCEL 대신
        /// 연결 끊김을 보게 되어, 취소 처리와 중단 시점 통계가 실행되지 않는다.
        ///
        /// pendingRead 는 취소 시점에 이미 진행 중이던 읽기 (없으면 null).
        /// 한도 안에 표시가 오지 않으면 회선이 죽은 것이므로 소켓을 닫아
        /// 진행 중인 읽기를 강제로 깨운다.
        /// </summary>
        private async Task CancelAndDrainAsync(Task<Frame> pendingRead, IProgress<string> log)
        {
            try
            {
                await SendFrameAsync(MsgType.Cancel, null, CancellationToken.None)
                          .ConfigureAwait(false);
                Log(log, "-> CANCEL            waiting for the server to acknowledge");
            }
            catch (Exception)
            {
                // 이미 끊긴 연결이라면 알릴 방법이 없다. 정상 경로다.
                ObserveAndDiscard(pendingRead);
                return;
            }

            Task<Frame> read = pendingRead;
            Task deadline = Task.Delay(CancelAckTimeoutMs);

            try
            {
                while (true)
                {
                    if (read == null)
                    {
                        read = ReadFrameAsync(CancellationToken.None);
                    }

                    Task first = await Task.WhenAny(read, deadline).ConfigureAwait(false);
                    if (first == deadline)
                    {
                        // 한도 안에 아무 표시도 오지 않았다 - 회선이 죽었을
                        // 가능성이 높다. 소켓을 닫아 진행 중인 읽기를 깨우고
                        // 연결을 정리한다 (유휴 감시 타이머가 곧 끊김을 반영한다).
                        Log(log, "   no cancel acknowledgement - closing the connection");
                        ObserveAndDiscard(read);
                        Dispose();
                        return;
                    }

                    Frame f = await read.ConfigureAwait(false);
                    read = null;

                    if (f.Type == MsgType.Error)
                    {
                        var r = new PayloadReader(f.Payload);
                        ushort code;
                        string msg;
                        if (r.U16(out code) && r.Str16(out msg))
                        {
                            Log(log, "<- ERROR({0})          {1}", code, msg);
                        }
                        return;
                    }
                    if (f.Type == MsgType.ResultEnd)
                    {
                        Log(log, "<- RESULT_END        (analysis finished before the cancel)");
                        return;
                    }

                    // 취소 직전까지 밀려 있던 진행 보고 / 결과 프레임. 버린다.
                }
            }
            catch (Exception ex)
            {
                Log(log, "   no cancel acknowledgement from the server ({0})",
                    ex.GetType().Name);
            }
        }

        /// <summary>정상 종료를 알린다. 실패해도 무시한다 - 어차피 닫을 것이다.</summary>
        public async Task TrySendByeAsync(CancellationToken ct)
        {
            try
            {
                await SendFrameAsync(MsgType.Bye, null, ct).ConfigureAwait(false);
            }
            catch (Exception)
            {
                // 이미 끊긴 연결에 BYE 를 보내려다 실패하는 것은 정상 경로다.
            }
        }

        /// <summary>
        /// 회선이 죽었는지 논블로킹으로 확인한다.
        ///
        /// Poll(SelectRead)이 true 인데 읽을 바이트가 0 이면 FIN/RST 가
        /// 도착한 것이다. 유휴 중에 데이터가 와 있으면(서버의 종료 통보 등)
        /// 그 뒤에 숨은 EOF 가 보이도록 버퍼를 비운 뒤 다시 확인한다.
        /// </summary>
        public bool IsLinkDead()
        {
            if (_disposed || _client == null || _client.Client == null)
            {
                return true;
            }

            try
            {
                Socket s = _client.Client;

                if (!s.Poll(0, SelectMode.SelectRead))
                {
                    return false;   // 조용한 소켓 = 살아 있는 유휴 연결
                }
                if (s.Available == 0)
                {
                    return true;    // 읽을 게 있다면서 0바이트 = 연결 종료
                }

                // 유휴 상태에 도착한 프레임(ERROR ShuttingDown 등)을 비운다.
                var scratch = new byte[4096];
                while (s.Available > 0)
                {
                    if (s.Receive(scratch) <= 0)
                    {
                        return true;
                    }
                }
                return s.Poll(0, SelectMode.SelectRead) && s.Available == 0;
            }
            catch (Exception)
            {
                // SocketException / ObjectDisposedException - 어느 쪽이든 죽은 회선이다.
                return true;
            }
        }

        private void EnsureUsable()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(ServerConnection));
            }
            if (_stream == null)
            {
                throw new InvalidOperationException("not connected");
            }
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;

            // 순서가 중요하다. 스트림을 먼저 닫아야 대기 중인 읽기가 깨어난다.
            if (_stream != null)
            {
                try { _stream.Close(); } catch (Exception) { }
                _stream.Dispose();
                _stream = null;
            }
            if (_client != null)
            {
                try { _client.Close(); } catch (Exception) { }
                _client = null;
            }
        }
    }
}
