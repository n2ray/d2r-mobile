// 存档导入/导出的回归：压缩-解压往返、截断包拒绝、原子替换与中断恢复。
// Save import/export regression: zip/unzip round trip, truncated archives, the atomic swap and interrupted replacement.
package org.d2r.android.port;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class SaveArchiveTest {
    private static int checks;

    private static void check(boolean condition, String message) {
        checks++;
        if (!condition) throw new AssertionError(message);
        System.out.println("  ✓ " + message);
    }

    private static File tempDir(String name) throws IOException {
        return Files.createTempDirectory("d2r-save-" + name).toFile();
    }

    private static void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("mkdirs " + parent);
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    private interface IoAction { void run() throws IOException; }

    private static boolean fails(IoAction action) {
        try { action.run(); return false; } catch (IOException expected) { return true; }
    }

    public static void main(String[] args) throws Exception {
        File save = tempDir("save");
        write(new File(save, "a.txt"), "alpha");
        write(new File(save, "sub/b.bin"), "beta");
        ByteArrayOutputStream exported = new ByteArrayOutputStream();
        int written = SaveArchive.zip(save, exported, 1700000000000L);
        check(written == 2, "导出写入 2 个文件（实际 " + written + "）");

        File restored = tempDir("restored");
        int extracted = SaveArchive.unzip(new ByteArrayInputStream(exported.toByteArray()), restored);
        check(extracted == 2, "解压恢复 2 个文件（实际 " + extracted + "）");
        check(read(new File(restored, "a.txt")).equals("alpha"), "顶层内容一致");
        check(read(new File(restored, "sub/b.bin")).equals("beta"), "子目录内容一致");
        check(!new File(restored, SaveArchive.MARKER).exists(), "说明文件不进入存档");

        // 截断包：有说明文件和一个完整条目、但没有中央目录，必须拒绝，不能把半包当成存档。
        // A truncated archive with the marker and one complete entry but no central directory must be rejected;
        // a partial package must not be treated as a save.
        ByteArrayOutputStream partialBytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(partialBytes)) {
            zip.putNextEntry(new ZipEntry(SaveArchive.MARKER));
            zip.write("{}".getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
            zip.putNextEntry(new ZipEntry("a.txt"));
            zip.write("alpha".getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
            zip.finish();
        }
        byte[] full = partialBytes.toByteArray();
        byte[] truncated = Arrays.copyOf(full, Math.max(0, full.length - 10));
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(truncated), tempDir("truncated"))),
                "截断的存档包被拒绝");

        // 替换成功：旧存档先移到备份，导入目录就位后旧备份删除。
        File swapParent = tempDir("swap");
        File live = new File(swapParent, "save");
        write(new File(live, "character.dat"), "old");
        File imported = new File(swapParent, "save.import");
        write(new File(imported, "character.dat"), "new");
        SaveArchive.replace(live, imported);
        check(read(new File(live, "character.dat")).equals("new"), "替换后是导入的存档");
        check(!new File(swapParent, "save.replaced").exists(), "成功替换不保留备份");
        check(!imported.exists(), "导入临时目录已就位");

        // 中断替换的恢复：上次在旧目录改名为 .replaced 后中断，live 不存在而 .replaced 仍完整。
        // 下一次替换即使导入包缺失，也必须先把旧存档恢复，再报错，不能删除唯一的副本。
        // Interrupted replacement recovery: the previous run moved the old directory to .replaced and stopped,
        // leaving live absent and .replaced intact. The next replacement, even with a missing import, must restore
        // the old save before reporting failure; it must not delete the only copy.
        File crashParent = tempDir("crash");
        File crashLive = new File(crashParent, "save");
        File crashBackup = new File(crashParent, "save.replaced");
        write(new File(crashBackup, "character.dat"), "keep-me");
        File missing = new File(crashParent, "save.import");
        check(fails(() -> SaveArchive.replace(crashLive, missing)), "导入包缺失时替换报错");
        check(read(new File(crashLive, "character.dat")).equals("keep-me"), "中断替换后旧存档恢复");
        check(!crashBackup.exists(), "恢复后不残留备份目录");

        // 前端在启动时可能已经建了空的 live 目录；恢复时必须认出“只有空目录”，不能把它当成新存档。
        // The front end may already have created an empty live directory at startup; recovery must recognise
        // "only an empty directory" instead of treating it as a new save.
        File emptyParent = tempDir("crash-empty");
        File emptyLive = new File(emptyParent, "save");
        check(emptyLive.mkdirs(), "建空的 live 目录");
        File emptyBackup = new File(emptyParent, "save.replaced");
        write(new File(emptyBackup, "character.dat"), "keep-empty");
        check(fails(() -> SaveArchive.replace(emptyLive, new File(emptyParent, "save.import"))),
                "空 live 下导入包缺失时替换报错");
        check(read(new File(emptyLive, "character.dat")).equals("keep-empty"), "空 live 下旧存档也恢复");
        check(!emptyBackup.exists(), "空 live 恢复后不残留备份");

        System.out.println("== " + checks + " 项全部通过 ==");
    }
}
