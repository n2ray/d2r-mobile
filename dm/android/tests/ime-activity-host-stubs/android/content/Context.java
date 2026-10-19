package android.content;

/** Narrow host-test Context boundary; production code is compiled against android.jar. */
public class Context {
    public static final String INPUT_METHOD_SERVICE = "input_method";

    private final android.content.res.Resources resources = new android.content.res.Resources();

    public android.content.res.Resources getResources() {
        return resources;
    }

    public Object getSystemService(String name) {
        return null;
    }
}
