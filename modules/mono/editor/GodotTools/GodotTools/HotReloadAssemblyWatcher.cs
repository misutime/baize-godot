using Godot;
using GodotTools.Build;
using GodotTools.Internals;
using JetBrains.Annotations;

namespace GodotTools
{
    public partial class HotReloadAssemblyWatcher : Node
    {
#nullable disable
        private Timer _watchTimer;
#nullable enable

        public override void _Notification(int what)
        {
            if (what == Node.NotificationWMWindowFocusIn)
            {
                RestartTimer();

                if (Internal.IsAssembliesReloadingNeeded())
                {
                    BuildManager.UpdateLastValidBuildDateTime();
                    Internal.ReloadAssemblies();
                }
            }
        }

        private void TimerTimeout()
        {
            if (Internal.IsAssembliesReloadingNeeded())
            {
                BuildManager.UpdateLastValidBuildDateTime();
                Internal.ReloadAssemblies();
            }
        }

        [UsedImplicitly]
        public void RestartTimer()
        {
            // FORK-CUSTOM：headless/命令行模式下 _watchTimer 可能未入场景树（_Ready 未完成），
            // Start() 会抛异常导致崩溃——空检查 + 树内检查兜底。
            if (_watchTimer == null || !IsNodeReady() || !_watchTimer.IsInsideTree())
            {
                return;
            }
            _watchTimer.Stop();
        }

        public override void _Ready()
        {
            base._Ready();

            _watchTimer = new Timer
            {
                OneShot = false,
                WaitTime = 0.5f
            };
            _watchTimer.Timeout += TimerTimeout;
            AddChild(_watchTimer);
            if (_watchTimer.IsInsideTree())
            {
                _watchTimer.Start();
            }
        }
    }
}
