# FFspider
![image](https://github.com/Cydral/FFspider/assets/53169060/532c096d-d06f-433c-902a-049985cd26c7)
<p><i>FFspider is a powerful web crawling and data extraction tool written in C++ using the Boost library. It allows you to efficiently crawl websites, extract valuable information, and perform various data processing tasks. Whether you need to scrape data, monitor websites for changes, or build your own web spider, FFspider provides a flexible and customizable solution.</i></p>

<h2>Description</h2>
<p>FFspider is a multi-threaded web crawler and data extraction tool designed for iterative site discovery. It is an all-in-one file that allows users to initiate site crawling starting from a provided URL. Please note that this crawler is relatively simple and does not meet the criteria of a "polite crawler" as it does not handle for instance load management on the target sites.</p>
<p>Initially created for internal needs, FFspider was developed to assist in the creation of image databases for AI research projects and the creation and the relevance evaluation of CNN models.</p>

<h2>Disclaimer</h2>
<p>Please exercise caution and adhere to privacy and legal regulations when using web scraping capabilities. Respect the terms of service and privacy policies of the websites you crawl, and ensure that you have the necessary permissions and rights to access and process the data. It is your responsibility to use this tool in a responsible and ethical manner.</p>

<h2>Features</h2>
<ul>
  <li>Multi-threaded crawling for improved performance.</li>
  <li>Flexible data extraction using CSS selectors and XPath.</li>
  <li><strong>TODO:</strong> Support for handling JavaScript-rendered pages using headless browsers.</li>
  <li>Extensible architecture for adding custom data processing and storage options.</li>
  <li>In-memory object database system to maximize performance during the crawling and processing of images.</li>
  <li>Automatically image storing during the crawling process to a local cache directory for future reuse.</li>
  <li>Configurable options for controlling crawling behavior.</li>
</ul>

<h2>Installation</h2>
<h3>Prerequisites</h3>
<p>The FFspider program has several external dependencies that need to be installed before use:</p>
<ul>
  <li><a href="https://github.com/google/gumbo-parser">Gumbo</a>: Gumbo is a library used for parsing HTML and extracting information from web pages.</li>
  <li><a href="https://github.com/fnc12/sqlite_orm">Sqlite_orm</a>: Sqlite_orm is a lightweight header-only C++ library for easy object-relational mapping (ORM) with SQLite.</li>
  <li><a href="https://www.boost.org/">Boost</a>: Boost provides various libraries for C++ programming, including utilities, algorithms, and data structures.</li>
  <li><a href="http://dlib.net/">Dlib</a>: Dlib is a general-purpose cross-platform C++ library that includes machine learning algorithms and tools for image processing.</li>
  <li><a href="https://github.com/whoshuu/cpr">Cpr</a>: Cpr is a C++ library for making HTTP requests.</li>
</ul>
<p>Please make sure to install these dependencies before proceeding with the FFspider program.</p>

<h3>Building FFspider</h3>
<p>FFspider is primarily designed to run on the Microsoft Windows platform (Windows 10 and above). The compilation process has been tested using Microsoft Visual Studio 2022. To build FFspider, please follow these steps:</p>
<ol>
  <li>Clone the FFspider repository from GitHub.</li>  
  <li>If you haven't already, install the vcpkg package manager: <code>git clone https://github.com/Microsoft/vcpkg.git</code></li>
  <li>Integrate vcpkg with Visual Studio: <code>vcpkg integrate install</code></li>
  <li>Install the required packages using vcpkg. For example, to install Gumbo and Sqlite_orm, use the following commands: <code>vcpkg install gumbo:x64-windows</code></li>
  <li>Create or open the project in Microsoft Visual Studio 2022.</li>
  <li>It is recommended to compile FFspider in x64 mode for optimal performance and compatibility.</li>
  <li>Build the FFspider project.</li>
</ol>
<p>After successful compilation and execution, you will be able to use FFspider for crawling websites and processing/storing images on a local machine.</p>

<h2>License</h2>
<p>FFspider is released under the <a href="https://github.com/your/repository/blob/main/LICENSE">MIT License</a>.</p>

<h2>Contact</h2>
<p>For any questions or feedback, feel free to reach out to us through the GitHub repository.</p>

<p align="center">Happy crawling with FFspider!</p>
