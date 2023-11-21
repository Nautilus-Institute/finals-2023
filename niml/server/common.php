<?

$_SERVER_ROOT = "/src/server/";

function normalizePath(&$path) {
    $patterns = array('~/{2,}~', '~/(\./)+~', '~([^/\.]+/(?R)*\.{2,}/)~', '~\.\./~');
    $replacements = array('/', '/', '', '');
    return preg_replace($patterns, $replacements, $path);
}

function server_info() {
// The client requires this to be correct to avoid SLA failure
?>
[head "Server Info" {id: "server-info-header"}]
[tag server_info
    [row name "Name: niMS"]
    [row version "Version: 0.0.1"]
    [row php_version "PHP Version: <?= phpversion() ?>"]
    [row flag [add "Flag: " [include "/flag"]]]
]
<?
}

function generate_header($page_name) {
?>
[style "style.css"]
[head "Home Center<? if (isset($page_name) && $page_name != "") { echo " -- " . $page_name; } ?>"]
[row header
    [button "Home" [[] [navigate "/index.php"]]]
    [button "Submit Post" [[] [navigate "/submit.php"]]]
<? if (isset($_SESSION["user"])) {
    if ($_SESSION["user"] == "admin") { ?>
        [button "Admin" [[] [navigate "/admin.php"]]]
    <? } ?>
    [button "Log-out of <?=$_SESSION["user"] ?>" [[] [navigate "/logout.php"]]]
<? } else { ?>
    [button "Log-in" [[] [navigate "/login.php"]]]
    [button "Register" [[] [navigate "/register.php"]]]
<? } ?>

]
<?
}

function prepare_body(&$str) {
    $str = str_replace("<", "⟨", $str);
    $str = str_replace(">", "⟩", $str);
    $str = str_replace("\\n", "\n", $str);
    return $str;
}
function prepare_title(&$str) {
    $str = str_replace("<", "⟨", $str);
    $str = str_replace(">", "⟩", $str);
    $str = str_replace("\"", "”", $str);
    $str = str_replace("'", "‛", $str);
    $str = str_replace("\n", " ", $str);
    $str = str_replace("\r", " ", $str);
    return $str;
}

function check_password_const(&$password, &$target_password) {
    $len = strlen($password);
    $password = str_split($password);
    for ($i = 0; $i < $len; $i++) {
        if ($target_password[$i] != $password[$i]) {
            $password[$i] = 1;
        } else {
            $password[$i] = 0;
        }
    }
    return array_sum($password) == 0;
}

?>