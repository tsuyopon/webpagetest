<?php
include 'common.inc';
$tid=$_GET['tid'];
$run=$_GET['run'];
?>

<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
    <head>
        <title>WebPagetest - Mobile Test</title>
        <?php $gaTemplate = 'Mobile Test'; include ('head.inc'); ?>
    </head>
    <body>
        <div class="page">
            <?php
            $tab = 'Home';
            $adLoc = 'Montreal_IE7';
            include 'header.inc';
            ?>
            <form name="mobileForm" action="http://www.blaze.io/runtest.php" method="GET">

            <h2 class="cufon-dincond_black">Test a website's performance</h2>

            <div id="test_box-container">
                <ul class="ui-tabs-nav">
                    <li class="analytical_review"><a href="/">Analytical Review</a></li>
                    <li class="visual_comparison"><a href="/video/">Visual Comparison</a></li>
                    <li class="mobile_test ui-state-default ui-corner-top ui-tabs-selected ui-state-active"><a href="javascript:void(0)">Mobile</a></li>
                </ul>
                <div id="mobile_test" class="test_box">
                    <p>Mobile testing is provided by <a href="http://www.blaze.io/mobile/">Mobitest</a> and you will be taken to the Blaze website for the results.</p>
                    <ul class="input_fields">
                        <li><input type="text" name="weburl" value="Enter Your Website URL"  id="url"  class="text large" onfocus="if (this.value == this.defaultValue) {this.value = '';}" onblur="if (this.value == '') {this.value = this.defaultValue;}" /></li>
                    </ul>
                    <div id="mobile_options">
                        <ul class="input_fields">
                            <li><input type="hidden" name="source" value="webpagetest" /></li>
                            <li>
                                <select name="device">
                                <option value="">Select a device</option> 
                                <option value="iphone43">iPhone 4.3</option>
                                <option value="android">Android 2.2</option>
                                <option value="nexus-s">Android 2.3</option>
                                </select>
                            </li>
                            <li>
                                <select name="numtest">
                                <option value="1">1 Run</option>
                                <option value="2">2 Runs</option>
                                <option value="3">3 Runs</option>
                                </select>
                            </li>
                            <li><input type="checkbox" name="video" value="1" /><label>Enable Video Capture?</label></li>
                            <li><input type="checkbox" name="private" value="private" /><label>Make Results Private?</label></li>
                        </ul>
                    </div>
                    <div id="mobile_logo">
                        <img src="<?php echo $GLOBALS['cdnPath']; ?>/images/mobitestlogo.png">
                    </div>
                    <div class="cleared"></div>
                </div>
            </div>

            <div id="start_test-container">
                <p><input type="submit" name="submit" value="" class="start_test"></p>
            </div>
            <div class="cleared"></div>
                
            </form>
            
            <?php include('footer.inc'); ?>
        </div>
    </body>
</html>
