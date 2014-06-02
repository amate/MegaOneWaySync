/* =================================
　　　　　　MegaOneWaySync
 =================================== */

■はじめに
このソフトはMega [https://mega.co.nz/] に、ローカルのファイルを一方的に同期するものです
ローカルのファイル/フォルダの構造がMegaに再現されます
ローカルに存在せず、Megaにだけ存在するファイル/フォルダは同期時に削除されるので注意してください

■使い方
起動して、オプションからログイン情報に、メールアドレスとパスワードを入力して"ログイン"を押してください
次回起動時には自動的にログインが施行されます
同期するフォルダを選択して、メインウィンドウの"同期"を押すと同期が開始されます

■既知のバグ
・ファイルサイズが0のファイルをMegaからダウンロードできない

■免責
作者(原著者＆改変者)は、このソフトによって生じた如何なる損害にも、
修正や更新も、責任を負わないこととします。
使用にあたっては、自己責任でお願いします。
 
何かあれば下記のURLにあるメールフォームにお願いします。
http://www31.atwiki.jp/lafe/pages/33.html

■著作権表示
Copyright (C) 2014 amate
Copyright (c) 1990, 2012 Oracle and/or its affiliates.  All rights reserved.

■使用ライブラリ
・MEGA SDK for C++
https://mega.co.nz/#sdk

・WTL
http://sourceforge.jp/projects/sfnet_wtl/releases/

・CDirectoryChangeWatcher
http://www.codeproject.com/Articles/950/CDirectoryChangeWatcher-ReadDirectoryChangesW-all

・Crypto++
http://sourceforge.jp/projects/sfnet_cryptopp/

・Berkeley DB 5.3.21
http://www.oracle.com/technetwork/jp/products/berkeleydb/overview/index.html

・FreeImage
http://sourceforge.jp/projects/sfnet_freeimage/
This software uses the FreeImage open source image library. See http://freeimage.sourceforge.net for details.
          FreeImage is used under the FIPL, version 1.0.


■ビルド方法
VisualStudio2013が必要です

https://mega.co.nz/#sdk から[Create an App]で AppKeyを取得してください。
取得したAppKeyは AppKey.hの"Write your AppKey"に書き込んでください。

WTL、Crypto++、Berkeley DB、FreeImageのそれぞれのライブラリをダウンロードして設定してください
