---
title: Classes

---

# Classes




* **namespace [@352013040131305060255325033333143145213225221002](Namespaces/namespace.md)** 
* **struct [FpuCtx](Classes/struct_fpu_ctx.md)** 
* **namespace [tk](Namespaces/namespacetk.md)** 
    * **class [Actor](Classes/classtk_1_1_actor.md)** <br>メッセージ駆動型タスク (アクター) の基底クラス 
    * **class [EventHub](Classes/classtk_1_1_event_hub.md)** <br>メッセージ型ごとの超軽量 Pub/Sub ディスパッチャ 
        * **struct [Subscriber](Classes/structtk_1_1_event_hub_1_1_subscriber.md)** 
    * **class [LockGuard](Classes/classtk_1_1_lock_guard.md)** <br>ミューテックスのロックをスコープ単位で安全に管理する RAII クラス (std::lock_guard 相当) 
    * **struct [Message](Classes/structtk_1_1_message.md)** <br>デフォルトの軽量メッセージ構造体 
    * **class [Mutex](Classes/classtk_1_1_mutex.md)** <br>所有権・優先度順ウェイクアップ・RAII 対応のミューテックス (排他ロック) 
    * **class [PreemptGuard](Classes/classtk_1_1_preempt_guard.md)** <br>プリエンプション禁止をスコープ単位で安全に管理する RAII クラス 
    * **class [Semaphore](Classes/classtk_1_1_semaphore.md)** <br>計数セマフォ (Counting [Semaphore]()) / バイナリセマフォ 
    * **struct [TaskHandle](Classes/structtk_1_1_task_handle.md)** <br>タスクの識別ハンドル 
    * **namespace [detail](Namespaces/namespacetk_1_1detail.md)** 



-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900
