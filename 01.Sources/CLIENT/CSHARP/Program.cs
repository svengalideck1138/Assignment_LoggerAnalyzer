using System;
using System.Windows.Forms;

namespace Individual_Assignment01_UI
{
    internal static class Program
    {
        /// <summary>
        /// 해당 애플리케이션의 주 진입점입니다.
        /// </summary>
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            // async void 이벤트 핸들러에서 새어 나온 예외(예: 창이 닫힌 뒤
            // 재개된 continuation)가 프로세스를 조용히 죽이지 않도록
            // 마지막 안전망을 둔다. 정상 경로에서는 도달하지 않는다.
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            Application.ThreadException += (sender, e) =>
                MessageBox.Show(
                    e.Exception.GetType().Name + ": " + e.Exception.Message,
                    "Zhenyu_LoggerAnalyzer - unexpected error",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);

            Application.Run(new Form1());
        }
    }
}
