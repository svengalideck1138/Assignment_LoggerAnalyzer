// 연결 생성 팩토리.
//
// 호출자는 구체 타입(ServerConnection)을 알 필요 없이 이 팩토리와
// IServerConnection 만 사용한다. 서버 쪽 ServerFactory 와 대응한다.

using System.Threading;
using System.Threading.Tasks;

namespace Individual_Assignment01_UI.Network
{
    internal static class ConnectionFactory
    {
        /// <summary>
        /// TCP 로 접속해 HELLO / HELLO_ACK 핸드셰이크까지 끝낸 연결을
        /// 돌려준다. 실패하면 예외를 던지고, 그 과정에서 만든 소켓은
        /// 전부 닫힌다.
        /// </summary>
        public static async Task<IServerConnection> ConnectTcpAsync(
            string host, int port, string clientName, int timeoutMs, CancellationToken ct)
        {
            return await ServerConnection.ConnectAsync(host, port, clientName, timeoutMs, ct)
                                         .ConfigureAwait(false);
        }
    }
}
