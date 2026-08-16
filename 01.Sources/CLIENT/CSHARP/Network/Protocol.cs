// Zhenyu_LoggerAnalyzer - wire protocol (client side)
//
// 서버의 src/net/Protocol.h 와 동일한 규약. 한쪽만 바뀌면 통신이 깨진다.
//
//   프레임 = 16바이트 고정 헤더(magic 'BYDA' u32 + version u8 + msg_type u8
//            + flags u16 + payload_len u64, 모두 big endian) + payload
//
// .NET FW 4.7.2 에는 BinaryPrimitives 가 없고 BitConverter 는 엔디안 의존이라
// 바이트 순서 변환을 직접 구현한다.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Individual_Assignment01_UI.Network
{
    internal enum MsgType : byte
    {
        Hello = 0x01,
        HelloAck = 0x02,

        UploadBegin = 0x10,
        UploadBeginAck = 0x11,
        UploadChunk = 0x12,
        UploadEnd = 0x13,
        UploadAck = 0x14,

        AnalyzeProgress = 0x20,
        AnalyzeDone = 0x21,

        ResultBegin = 0x31,
        ResultChunk = 0x32,
        ResultEnd = 0x33,

        Cancel = 0x40,
        Error = 0x7E,
        Bye = 0x7F,
    }

    internal enum ProtocolError : ushort
    {
        None = 0,
        BadMagic = 1,
        BadVersion = 2,
        UnexpectedType = 3,
        PayloadTooLarge = 4,
        SizeMismatch = 5,
        InternalIo = 6,
        IdleTimeout = 8,
        Cancelled = 9,
        ServerBusy = 10,
        ShuttingDown = 11,
    }

    internal static class Wire
    {
        public const uint Magic = 0x42594441u;
        public const byte Version = 0x01;
        public const int HeaderSize = 16;
        public const long MaxPayload = 4L * 1024 * 1024;

        public const ushort ClientProtocolVersion = 1;

        public static string Name(MsgType t)
        {
            switch (t)
            {
                case MsgType.Hello: return "HELLO";
                case MsgType.HelloAck: return "HELLO_ACK";
                case MsgType.UploadBegin: return "UPLOAD_BEGIN";
                case MsgType.UploadBeginAck: return "UPLOAD_BEGIN_ACK";
                case MsgType.UploadChunk: return "UPLOAD_CHUNK";
                case MsgType.UploadEnd: return "UPLOAD_END";
                case MsgType.UploadAck: return "UPLOAD_ACK";
                case MsgType.AnalyzeProgress: return "ANALYZE_PROGRESS";
                case MsgType.AnalyzeDone: return "ANALYZE_DONE";
                case MsgType.ResultBegin: return "RESULT_BEGIN";
                case MsgType.ResultChunk: return "RESULT_CHUNK";
                case MsgType.ResultEnd: return "RESULT_END";
                case MsgType.Cancel: return "CANCEL";
                case MsgType.Error: return "ERROR";
                case MsgType.Bye: return "BYE";
                default: return "0x" + ((byte)t).ToString("X2");
            }
        }

        public static string ErrorText(ProtocolError c)
        {
            switch (c)
            {
                case ProtocolError.None: return "none";
                case ProtocolError.BadMagic: return "bad magic";
                case ProtocolError.BadVersion: return "unsupported protocol version";
                case ProtocolError.UnexpectedType: return "unexpected message type";
                case ProtocolError.PayloadTooLarge: return "payload too large";
                case ProtocolError.SizeMismatch: return "declared size mismatch";
                case ProtocolError.InternalIo: return "internal io error";
                case ProtocolError.IdleTimeout: return "idle timeout";
                case ProtocolError.Cancelled: return "cancelled";
                case ProtocolError.ServerBusy: return "server busy";
                case ProtocolError.ShuttingDown: return "server shutting down";
                default: return "unknown (" + (ushort)c + ")";
            }
        }

        public static void StoreBe16(byte[] buf, int at, ushort v)
        {
            buf[at + 0] = (byte)((v >> 8) & 0xFF);
            buf[at + 1] = (byte)(v & 0xFF);
        }

        public static void StoreBe32(byte[] buf, int at, uint v)
        {
            buf[at + 0] = (byte)((v >> 24) & 0xFF);
            buf[at + 1] = (byte)((v >> 16) & 0xFF);
            buf[at + 2] = (byte)((v >> 8) & 0xFF);
            buf[at + 3] = (byte)(v & 0xFF);
        }

        public static void StoreBe64(byte[] buf, int at, ulong v)
        {
            for (int i = 0; i < 8; i++)
            {
                buf[at + i] = (byte)((v >> (56 - 8 * i)) & 0xFF);
            }
        }

        public static ushort LoadBe16(byte[] buf, int at)
        {
            return (ushort)((buf[at] << 8) | buf[at + 1]);
        }

        public static uint LoadBe32(byte[] buf, int at)
        {
            return ((uint)buf[at] << 24) | ((uint)buf[at + 1] << 16)
                 | ((uint)buf[at + 2] << 8) | buf[at + 3];
        }

        public static ulong LoadBe64(byte[] buf, int at)
        {
            ulong v = 0;
            for (int i = 0; i < 8; i++)
            {
                v = (v << 8) | buf[at + i];
            }
            return v;
        }

        /// <summary>바이트 수를 사람이 읽기 좋은 문자열로 ("482.8 MiB").</summary>
        public static string HumanBytes(long n)
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

        public static byte[] EncodeHeader(MsgType type, long payloadLength)
        {
            var head = new byte[HeaderSize];
            StoreBe32(head, 0, Magic);
            head[4] = Version;
            head[5] = (byte)type;
            StoreBe16(head, 6, 0);
            StoreBe64(head, 8, (ulong)payloadLength);
            return head;
        }
    }

    /// <summary>수신한 프레임 하나.</summary>
    internal sealed class Frame
    {
        public MsgType Type;
        public ushort Flags;
        public byte[] Payload = new byte[0];
    }

    /// <summary>payload 를 순서대로 쌓는다.</summary>
    internal sealed class PayloadWriter
    {
        private readonly List<byte> _buf = new List<byte>(64);

        public void U8(byte v) { _buf.Add(v); }

        public void U16(ushort v)
        {
            var t = new byte[2];
            Wire.StoreBe16(t, 0, v);
            _buf.AddRange(t);
        }

        public void U32(uint v)
        {
            var t = new byte[4];
            Wire.StoreBe32(t, 0, v);
            _buf.AddRange(t);
        }

        public void U64(ulong v)
        {
            var t = new byte[8];
            Wire.StoreBe64(t, 0, v);
            _buf.AddRange(t);
        }

        /// <summary>길이(u16) 접두 UTF-8 문자열.</summary>
        public void Str16(string s)
        {
            byte[] b = Encoding.UTF8.GetBytes(s ?? string.Empty);
            if (b.Length > ushort.MaxValue)
            {
                Array.Resize(ref b, ushort.MaxValue);
            }
            U16((ushort)b.Length);
            _buf.AddRange(b);
        }

        public byte[] ToArray() { return _buf.ToArray(); }
    }

    /// <summary>
    /// payload 를 순서대로 읽는다. 남은 바이트가 모자라면 그 시점부터
    /// 모든 읽기가 실패로 고정된다. 잘리거나 조작된 프레임이 와도
    /// IndexOutOfRange 로 터지지 않는다.
    /// </summary>
    internal sealed class PayloadReader
    {
        private readonly byte[] _buf;
        private int _pos;
        private bool _ok = true;

        public PayloadReader(byte[] buf) { _buf = buf ?? new byte[0]; }

        public bool Ok { get { return _ok; } }
        public int Remaining { get { return _buf.Length - _pos; } }

        private bool Need(int n)
        {
            if (!_ok || Remaining < n)
            {
                _ok = false;
                return false;
            }
            return true;
        }

        public bool U8(out byte v)
        {
            v = 0;
            if (!Need(1)) return false;
            v = _buf[_pos];
            _pos += 1;
            return true;
        }

        public bool U16(out ushort v)
        {
            v = 0;
            if (!Need(2)) return false;
            v = Wire.LoadBe16(_buf, _pos);
            _pos += 2;
            return true;
        }

        public bool U32(out uint v)
        {
            v = 0;
            if (!Need(4)) return false;
            v = Wire.LoadBe32(_buf, _pos);
            _pos += 4;
            return true;
        }

        public bool U64(out ulong v)
        {
            v = 0;
            if (!Need(8)) return false;
            v = Wire.LoadBe64(_buf, _pos);
            _pos += 8;
            return true;
        }

        public bool Str16(out string s)
        {
            s = string.Empty;
            ushort n;
            if (!U16(out n)) return false;
            if (!Need(n)) return false;
            s = Encoding.UTF8.GetString(_buf, _pos, n);
            _pos += n;
            return true;
        }
    }

    /// <summary>HELLO_ACK 내용.</summary>
    internal sealed class HelloAck
    {
        public uint SessionId;
        public uint MaxChunk;
        public string ObservedPeer = string.Empty;
        public string ServerVersion = string.Empty;
    }

    /// <summary>
    /// 업로드 진행 상황. BytesSent(보낸 양)와 BytesConsumed(서버가 파싱까지
    /// 끝낸 양)를 구분해 보여주면 "바가 100% 인데 안 끝남" 혼란이 사라진다.
    /// </summary>
    internal sealed class UploadProgress
    {
        public long TotalBytes;
        public long BytesSent;

        // ---- 서버가 UPLOAD_ACK 으로 알려주는 진행 중 분석 결과 ----
        public long BytesConsumed;
        public long LinesParsed;
        public long AcceptedLines;
        public long RejectedLines;
        public long SpdSamples;
        public string SpdAverage = string.Empty;
        public string[] ModuleNames = new string[0];
        public long[] ModuleCounts = new long[0];

        /// <summary>서버 통계가 갱신된 횟수. UI 가 다시 그릴지 판단하는 데 쓴다.</summary>
        public int StatsRevision;

        /// <summary>
        /// 워커 스레드가 계속 갱신하는 객체를 그대로 UI 로 넘기면 값이
        /// 읽히는 도중에 바뀔 수 있다. 보고할 때마다 복사본을 만든다.
        /// </summary>
        public UploadProgress Snapshot()
        {
            return new UploadProgress
            {
                TotalBytes = TotalBytes,
                BytesSent = BytesSent,
                BytesConsumed = BytesConsumed,
                LinesParsed = LinesParsed,
                AcceptedLines = AcceptedLines,
                RejectedLines = RejectedLines,
                SpdSamples = SpdSamples,
                SpdAverage = SpdAverage,
                ModuleNames = ModuleNames,      // 한 번 정해지면 바뀌지 않는다
                ModuleCounts = (long[])ModuleCounts.Clone(),
                StatsRevision = StatsRevision,
            };
        }
    }

    /// <summary>ANALYZE_DONE 내용과 뒤이어 받은 result.csv.</summary>
    internal sealed class AnalyzeResult
    {
        public long TotalBytes;
        public long TotalLines;
        public long AcceptedLines;
        public long RejectedLines;
        public long OversizeLines;
        public long SpdSamples;
        public string SpdAverage = string.Empty;
        public long ElapsedMs;
        public long PeakRssKb;
        public long CsvSize;

        /// <summary>서버가 보내온 result.csv 원문.</summary>
        public string Csv = string.Empty;
    }

    /// <summary>
    /// result.csv 의 한 섹션 ("[SECTION] NAME" 줄 + 헤더 + 빈 줄까지의 데이터).
    /// 화면의 콤보박스/그리드가 이 구조를 그대로 쓴다.
    /// </summary>
    internal sealed class CsvSection
    {
        public string Name = string.Empty;
        public string[] Header = new string[0];
        public List<string[]> Rows = new List<string[]>();
    }

    internal static class CsvDocument
    {
        /// <summary>섹션 목록으로 분해한다. 파싱에 실패해도 예외를 던지지 않는다.</summary>
        public static List<CsvSection> Parse(string csv)
        {
            var sections = new List<CsvSection>();
            if (string.IsNullOrEmpty(csv))
            {
                return sections;
            }

            string[] lines = csv.Replace("\r\n", "\n").Split('\n');
            CsvSection current = null;
            bool expectHeader = false;

            foreach (string raw in lines)
            {
                string line = raw;

                if (line.StartsWith("[SECTION] ", StringComparison.Ordinal))
                {
                    current = new CsvSection { Name = line.Substring(10).Trim() };
                    sections.Add(current);
                    expectHeader = true;
                    continue;
                }

                if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal))
                {
                    continue;
                }

                if (current == null)
                {
                    continue;
                }

                string[] fields = SplitLine(line);
                if (expectHeader)
                {
                    current.Header = fields;
                    expectHeader = false;
                }
                else
                {
                    current.Rows.Add(fields);
                }
            }

            return sections;
        }

        /// <summary>RFC 4180 인용 규칙을 지키며 한 줄을 필드로 나눈다.</summary>
        public static string[] SplitLine(string line)
        {
            var fields = new List<string>();
            var sb = new StringBuilder();
            bool inQuotes = false;

            for (int i = 0; i < line.Length; i++)
            {
                char c = line[i];

                if (inQuotes)
                {
                    if (c == '"')
                    {
                        // 연속된 큰따옴표는 리터럴 한 개를 뜻한다.
                        if (i + 1 < line.Length && line[i + 1] == '"')
                        {
                            sb.Append('"');
                            i++;
                        }
                        else
                        {
                            inQuotes = false;
                        }
                    }
                    else
                    {
                        sb.Append(c);
                    }
                }
                else if (c == '"')
                {
                    inQuotes = true;
                }
                else if (c == ',')
                {
                    fields.Add(sb.ToString());
                    sb.Length = 0;
                }
                else
                {
                    sb.Append(c);
                }
            }

            fields.Add(sb.ToString());
            return fields.ToArray();
        }
    }
}
