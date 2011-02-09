<?php 
include 'common.inc';
require_once('page_data.inc');
$pageData = loadAllPageData($testPath);

// if we don't have an url, try to get it from the page results
if( !strlen($url) )
    $url = $pageData[1][0]['URL'];

// see if it is one of the ancient tests before we moved to php
if( is_file("$testPath/testinfo.json") )
    $test['test']['completeTime'] = date("F d Y H:i:s.", filemtime("$testPath/testinfo.json"));

if( isset($test['test']) && $test['test']['batch'] )
    include 'resultBatch.inc';
elseif( isset($test['testinfo']['cancelled']) )
    include 'testcancelled.inc';
elseif( (isset($test['test']) && isset($test['test']['completeTime'])) || count($pageData) > 0 )
{
    if( $test['test']['type'] == 'traceroute' )
        include 'resultTraceroute.inc';
    else
        include 'result.inc';
}
else
    include 'running.inc';
?>
