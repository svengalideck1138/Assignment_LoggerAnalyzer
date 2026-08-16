// 서버 연결 추상 인터페이스. UI 는 이것만 다루고 구현은 팩토리가 숨긴다
// (서버 쪽 ITcpServer 와 같은 원칙). IDisposable 상속으로 using 결정적
// 해제가 가능하다 — C++ RAII 의 C# 등가물.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Individual_Assignment01_UI.Network
{
    internal interface IServerConnection : IDisposable
    {
        /// <summary>HELLO_ACK 로 받은 세션 정보 (세션 id, 서버가 본 내 주소 등).</summary>
        HelloAck Ack { get; }

        string LocalEndpoint { get; }
        string RemoteEndpoint { get; }

        bool IsConnected { get; }

        /// <summary>
        /// 파일을 서버로 스트리밍하고 분석 결과(result.csv 포함)를 받는다.
        /// 진행률은 progress 로, 사람이 읽을 로그는 log 로 흘러나온다.
        /// </summary>
        Task<AnalyzeResult> UploadAsync(
            string path, IProgress<UploadProgress> progress, IProgress<string> log,
            CancellationToken ct);

        /// <summary>정상 종료 인사. 실패해도 예외를 던지지 않는다.</summary>
        Task TrySendByeAsync(CancellationToken ct);

        /// <summary>
        /// 회선이 죽었는지 논블로킹으로 확인한다. TCP 는 끊겨도 앱에
        /// 통보해 주지 않으므로, 유휴 상태에서는 이걸 주기적으로 불러야
        /// 서버 종료를 제때 알아챌 수 있다. 전송 중에는 부르지 말 것
        /// (업로드 경로가 자체적으로 오류를 감지한다).
        /// </summary>
        bool IsLinkDead();
    }
}
