<?

include_once("common.php");

$_SESSION = array();
$_ARGS = array();

function __read_file($name) {
    global $_SERVER_ROOT;
    return file_get_contents($_SERVER_ROOT . normalizePath($name));
}

function __read_request() {
    // Read a single line from stdin ending with \n
    $line = fgets(STDIN, 0x10000);
    return rtrim($line, "\n");
}

function __view_page($name, $args) {
    global $_SERVER_ROOT;
    global $_SESSION;
    global $_ARGS;

    ini_set('display_errors', 1);
    ini_set('display_startup_errors', 1);
    error_reporting(E_ALL);

    if ($name == "" || $name == "/") {
        $name = "/index.php";
    }

    if (isset($args) && !is_null($args)) {
        // Update args but keep old ones too
        $_ARGS = array();
        foreach ($args as $key => $value) {
            $_ARGS[$key] = $value;
        }
    }

    ob_start();
    include($_SERVER_ROOT . normalizePath($name));
    //include('/src/server/' . $name);
    return ob_get_clean();
}

if (isset($argc) && count($argv) > 1) {
    $url = $argv[1];
    $url = parse_url($url);
    $args = array();
    if (count($argv) > 2) {
        $args = json_decode($argv[2], true);
    } else if (isset($url["query"]) && $url["query"] != "") {
        parse_str($url["query"], $args);
    }

    $admin_password = trim(file_get_contents("/flag"));
    $admin_password = "admin"; // TODO remove
    //NIMS_view_request("/login.php", array("username" => "admin", "password" => $admin_password));

    $res = NIMS_view_request($url["path"], $args);
    echo $res;
    die();
}

NIMS_start_server(null);

?>