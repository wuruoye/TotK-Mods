// Copyright (c) 2026 wuruoye. Licensed under the MIT License.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

public static class KorokStartSetup {
    const string Relative = "Banc/MainField/HiddenKorok/MainField.hiddenkorok.byml.zs";
    const string Folder = "Korok Start Positions 1.4.3";
    const uint OriginalRaw = 0x8A04BF6E, PatchedRaw = 0x2C0367D3;
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern uint ZSTD_versionNumber();
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern ulong ZSTD_getFrameContentSize(byte[] src, UIntPtr size);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern uint ZSTD_getDictID_fromFrame(byte[] src, UIntPtr size);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern uint ZSTD_getDictID_fromDict(byte[] src, UIntPtr size);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ZSTD_createDCtx();
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern UIntPtr ZSTD_freeDCtx(IntPtr ctx);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ZSTD_createCCtx();
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern UIntPtr ZSTD_freeCCtx(IntPtr ctx);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern UIntPtr ZSTD_compressBound(UIntPtr size);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern uint ZSTD_isError(UIntPtr code);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern UIntPtr ZSTD_decompress_usingDict(IntPtr ctx, byte[] dst, UIntPtr capacity, byte[] src, UIntPtr size, byte[] dict, UIntPtr dictSize);
    [DllImport("libzstd.dll", CallingConvention=CallingConvention.Cdecl)] static extern UIntPtr ZSTD_compress_usingDict(IntPtr ctx, byte[] dst, UIntPtr capacity, byte[] src, UIntPtr size, byte[] dict, UIntPtr dictSize, int level);
    static UIntPtr Size(byte[] data) { return (UIntPtr)(uint)(data == null ? 0 : data.Length); }
    static void Require(bool condition, string message) { if(!condition) throw new InvalidDataException(message); }
    static uint Crc(byte[] bytes) {
        uint value=0xffffffff;
        foreach(byte b in bytes) { value^=b;for(int i=0;i<8;i++)value=(value>>1)^((value&1)!=0 ? 0xedb88320u : 0u); }
        return value^0xffffffff;
    }
    static byte[] Decode(byte[] packed, Dictionary<uint,byte[]> dictionaries) {
        ulong size=ZSTD_getFrameContentSize(packed,Size(packed));Require(size>0 && size<=16777216,"资源文件大小不受支持。");
        uint id=ZSTD_getDictID_fromFrame(packed,Size(packed));byte[] dict=null;
        Require(id==0 || dictionaries.TryGetValue(id,out dict),"缺少游戏资源的压缩字典。");
        var result=new byte[(int)size];IntPtr context=ZSTD_createDCtx();Require(context!=IntPtr.Zero,"无法初始化解压组件。");
        try { UIntPtr count=ZSTD_decompress_usingDict(context,result,Size(result),packed,Size(packed),dict,Size(dict));Require(ZSTD_isError(count)==0 && count.ToUInt64()==size,"资源解压失败。"); }
        finally { ZSTD_freeDCtx(context); }
        return result;
    }
    static byte[] Encode(byte[] raw, byte[] dict) {
        var result=new byte[(int)ZSTD_compressBound(Size(raw)).ToUInt64()];IntPtr context=ZSTD_createCCtx();Require(context!=IntPtr.Zero,"无法初始化压缩组件。");
        try { UIntPtr count=ZSTD_compress_usingDict(context,result,Size(result),raw,Size(raw),dict,Size(dict),19);Require(ZSTD_isError(count)==0,"资源压缩失败。");Array.Resize(ref result,(int)count.ToUInt64()); }
        finally { ZSTD_freeCCtx(context); }
        return result;
    }
    static byte[] Slice(byte[] raw,int start,int length) { Require(start>=0 && length>=0 && (long)start+length<=raw.Length,"资源结构不完整。");var result=new byte[length];Buffer.BlockCopy(raw,start,result,0,length);return result; }
    static string Input(string root,string relative) {
        string direct=Path.Combine(root,relative.Replace('/',Path.DirectorySeparatorChar));if(File.Exists(direct))return direct;
        string nested=Path.Combine(root,"romfs",relative.Replace('/',Path.DirectorySeparatorChar));Require(File.Exists(nested),"所选文件夹缺少："+relative);return nested;
    }
    static Dictionary<uint,byte[]> ReadDictionaries(string source) {
        byte[] packed=File.ReadAllBytes(Input(source,"Pack/ZsDic.pack.zs"));Require(Crc(packed)==0xE7993BC2,"压缩字典版本不匹配，请选择原版 1.4.3 RomFS。");
        byte[] raw=Decode(packed,new Dictionary<uint,byte[]>());Require(raw.Length>=40 && Encoding.ASCII.GetString(raw,0,4)=="SARC" && BitConverter.ToUInt16(raw,6)==0xfeff,"压缩字典包格式不受支持。");
        int data=checked((int)BitConverter.ToUInt32(raw,12)),count=BitConverter.ToUInt16(raw,26);Require(count<100,"压缩字典包成员数量无效。");var result=new Dictionary<uint,byte[]>();
        for(int i=0;i<count;i++) { int entry=32+i*16;Require(entry+16<=raw.Length,"字典包不完整。");uint start=BitConverter.ToUInt32(raw,entry+8),end=BitConverter.ToUInt32(raw,entry+12);byte[] dict=Slice(raw,checked(data+(int)start),checked((int)(end-start)));uint id=ZSTD_getDictID_fromDict(dict,Size(dict));Require(id!=0 && !result.ContainsKey(id),"字典标识无效。");result.Add(id,dict); }
        return result;
    }
    public static string Generate(string source,string output) {
        Require(ZSTD_versionNumber()==10507,"请使用安装包内的 Zstandard 1.5.7 组件。");
        source=Path.GetFullPath(source);output=Path.GetFullPath(output);Require(Directory.Exists(source),"游戏资源文件夹不存在。");
        var dictionaries=ReadDictionaries(source);byte[] packed=File.ReadAllBytes(Input(source,Relative));
        Require(Crc(packed)==0xA4B83FA6,"地图资源版本不匹配，请选择原版 1.4.3 RomFS。");
        byte[] before=Decode(packed,dictionaries);Require(before.Length==47220 && Crc(before)==OriginalRaw,"原版地图资源内容不匹配。");
        byte[] after=(byte[])before.Clone();byte[] patch;
        using(Stream stream=typeof(KorokStartSetup).Assembly.GetManifestResourceStream("starts.patch")) {
            Require(stream!=null,"工具缺少坐标补丁。");using(var memory=new MemoryStream()) { stream.CopyTo(memory);patch=memory.ToArray(); }
        }
        using(var reader=new BinaryReader(new MemoryStream(patch))) {
            Require(Encoding.ASCII.GetString(reader.ReadBytes(8))=="KSTART01" && reader.ReadUInt32()==47220 && reader.ReadUInt32()==OriginalRaw && reader.ReadUInt32()==PatchedRaw,"坐标补丁头不匹配。");
            uint count=reader.ReadUInt32();Require(count==2700 && reader.BaseStream.Length==24+count*8,"坐标补丁大小不匹配。");var seen=new HashSet<uint>();
            for(int i=0;i<count;i++) { uint at=reader.ReadUInt32(),bits=reader.ReadUInt32();Require(at%4==0 && at<=after.Length-4 && seen.Add(at),"坐标补丁字段范围无效。");Buffer.BlockCopy(BitConverter.GetBytes(bits),0,after,(int)at,4); }
        }
        Require(Crc(after)==PatchedRaw,"坐标补丁生成结果不匹配。");uint dictionaryId=ZSTD_getDictID_fromFrame(packed,Size(packed));
        byte[] encoded=Encode(after,dictionaries[dictionaryId]);Require(Crc(encoded)==0x734184E8 && Decode(encoded,dictionaries).SequenceEqual(after),"最终坐标资源核对失败。");
        Require(!File.Exists(output),"输出位置是文件，请选择文件夹。");string mod=Path.Combine(output,Folder),target=Path.Combine(mod,"romfs",Relative.Replace('/',Path.DirectorySeparatorChar));
        if(Directory.Exists(output)) foreach(string directory in Directory.GetDirectories(output)) {
            string other=Path.Combine(directory,"romfs",Relative.Replace('/',Path.DirectorySeparatorChar));
            Require(!File.Exists(other) || String.Equals(Path.GetFullPath(other),Path.GetFullPath(target),StringComparison.OrdinalIgnoreCase),"其他 Mod 已覆盖呀哈哈坐标表，请先处理冲突："+Path.GetFileName(directory));
        }
        if(Directory.Exists(mod)) {
            Require(File.Exists(target) && File.ReadAllBytes(target).SequenceEqual(encoded) && Directory.GetFiles(mod,"*",SearchOption.AllDirectories).Length==1,"目标 Mod 文件夹已存在且内容不同，请改用空文件夹。");return target;
        }
        if(Directory.Exists(output)) { Require((new DirectoryInfo(output).Attributes&FileAttributes.ReparsePoint)==0,"输出目录不能是链接。");Require(Directory.GetFileSystemEntries(output).Length==0,"生成位置需要是空文件夹。请生成后再复制到游戏 Mod 目录。"); }
        Directory.CreateDirectory(Path.GetDirectoryName(target));string temporary=target+".tmp";
        try { using(var stream=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None)) { stream.Write(encoded,0,encoded.Length);stream.Flush(true); }Require(File.ReadAllBytes(temporary).SequenceEqual(encoded),"写入后核对失败。");File.Move(temporary,target); }
        catch { if(File.Exists(temporary))File.Delete(temporary);throw; }
        return target;
    }
    [STAThread] public static int Main(string[] args) {
        if(args.Length!=0) { try { Require(args.Length==3 && args[0]=="--generate","用法：Korok-Start-Setup.exe --generate RomFS文件夹 输出文件夹");Console.WriteLine(Generate(args[1],args[2]));return 0; } catch(Exception error) { Console.Error.WriteLine(error.Message);return 1; } }
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new SetupWindow());return 0;
    }
}

public sealed class SetupWindow : Form {
    readonly TextBox source=new TextBox(),output=new TextBox();readonly Button generate=new Button();readonly Label state=new Label();
    public SetupWindow() {
        Text="呀哈哈起点坐标补丁 1.0.0";Font=new Font("Microsoft YaHei UI",10);ClientSize=new Size(720,360);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
        var intro=new Label { Text="将 800 个普通呀哈哈和 100 组搬运任务的标记移到操作入口。\n需要原版 1.4.3 RomFS，以及兼容的未完成呀哈哈地图显示 Mod。",Left=24,Top=20,Width=670,Height=62 };Controls.Add(intro);
        AddRow("原版 1.4.3 RomFS",source,100);AddRow("生成位置（空文件夹）",output,172);
        generate.Text="生成 Mod 文件夹";generate.SetBounds(472,264,220,42);generate.Click+=delegate { generate.Enabled=false;state.Text="正在核对资源并生成……";Application.DoEvents();try { string target=KorokStartSetup.Generate(source.Text,output.Text);state.Text="生成完成，可按说明复制安装。";MessageBox.Show(this,"已生成：\n"+Path.Combine(output.Text,"Korok Start Positions 1.4.3")+"\n\n保存并关闭游戏，再将生成的文件夹复制到游戏 Mod 目录。","生成完成",MessageBoxButtons.OK,MessageBoxIcon.Information); } catch(Exception error) { state.Text="未完成，请检查输入或目录冲突。";MessageBox.Show(this,error.Message,"无法生成",MessageBoxButtons.OK,MessageBoxIcon.Warning); } finally { generate.Enabled=true; } };Controls.Add(generate);
        state.Text="选择本地游戏资源和输出文件夹。";state.SetBounds(24,270,425,52);Controls.Add(state);
    }
    void AddRow(string text,TextBox field,int top) { var label=new Label { Text=text,Left=24,Top=top,Width=640,Height=24 };field.SetBounds(24,top+27,565,28);field.ReadOnly=true;var browse=new Button { Text="选择文件夹",Left=601,Top=top+25,Width=91,Height=32 };browse.Click+=delegate { using(var dialog=new FolderBrowserDialog()) { dialog.Description=text;if(dialog.ShowDialog(this)==DialogResult.OK)field.Text=dialog.SelectedPath; } };Controls.Add(label);Controls.Add(field);Controls.Add(browse); }
}
