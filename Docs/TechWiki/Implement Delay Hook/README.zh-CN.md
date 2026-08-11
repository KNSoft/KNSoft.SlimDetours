| [English (en-US)](./README.md) | **简体中文 (zh-CN)** |
| --- | --- |

&nbsp;

# 实现延迟挂钩

## 什么是“延迟挂钩”和它带来的好处？

通常挂钩DLL中函数的做法是先将对应的DLL加载到进程地址空间，并定位函数地址（例如，使用[`LoadLibraryW`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw) + [`GetProcAddress`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress)）。

对于为特定程序设计的钩子，通常它们的目标函数迟早会被调用，DLL也是进程需要的，所以早些加载对应的DLL通常可以接受。而对于被注入到不同进程中的钩子（尤其是全局钩子），它们不知道各个进程是否需要此DLL，所以通常仍会将DLL加载到每个进程地址空间并挂钩函数，即使进程本身并不需要这个DLL。

试想一下，一个有不少依赖项的全局钩子试图挂钩各种系统DLL的函数，就会将所有涉及的DLL都带入到每个进程进行加载和初始化，开销极大。

“延迟挂钩”是此问题的一个好方案。即如果目标DLL已加载则立即执行挂钩，否则等到目标DLL加载到进程的时候挂钩。

## 技术方案

显然，实现“延迟挂钩”的关键是尽早收到DLL加载通知。“[DLL加载通知](https://learn.microsoft.com/en-us/windows/win32/devnotes/dll-load-notification)”机制自NT6被引入，这正是我们需要的。

参考[LdrRegisterDllNotification](https://learn.microsoft.com/en-us/windows/win32/devnotes/ldrregisterdllnotification)函数，DLL加载（与卸载）通知将被发送给由此函数注册的回调。此时，DLL映射出的内存区域已经可用，因此我们可以进行挂钩。

尽管Microsoft Learn提示相关API可能会被更改或删除，但它们的用法一直没变。自NT6.1起，只有所持的锁由`LdrpLoaderLock`变为了专用的`LdrpDllNotificationLock`。总之，请保持回调尽可能简单。

> [!TIP]
> 如果你想了解Windows上“[DLL加载通知](https://learn.microsoft.com/en-us/windows/win32/devnotes/dll-load-notification)”的内部实现，参考我为[ReactOS](https://github.com/reactos/reactos)贡献的[ReactOS PR #6795](https://github.com/reactos/reactos/pull/6795)。不要参考[WINE的实现](https://gitlab.winehq.org/wine/wine/-/commit/4c13e1765f559b322d8c071b2e23add914981db7)，因为它截至此文编写时存在错误，例如，它的`LdrUnregisterDllNotification`没有检查节点是否处于链表中就进行了移除。

[示例：DelayHook](../../../Source/Demo/DelayHook.c)演示了使用此机制实现在DLL加载时挂钩其中函数。

<br>
<hr>

本作品采用 [知识共享署名-非商业性使用-相同方式共享 4.0 国际许可协议 (CC BY-NC-SA 4.0)](http://creativecommons.org/licenses/by-nc-sa/4.0/) 进行许可。  
<br>
**[Ratin](https://github.com/RatinCN) &lt;[ratin@knsoft.org](mailto:ratin@knsoft.org)&gt;**  
*中国国家认证系统架构设计师*  
*[ReactOS](https://github.com/reactos/reactos)贡献者*
