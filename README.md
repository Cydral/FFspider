# FFspider
![image](https://github.com/Cydral/FFspider/assets/53169060/532c096d-d06f-433c-902a-049985cd26c7)
<p><i>FFspider is a powerful web crawling and data extraction tool written in C++ using the Boost library. It allows you to efficiently crawl websites, extract valuable information, and perform various data processing tasks. Whether you need to scrape data, monitor websites for changes, or build your own web spider, FFspider provides a flexible and customizable solution.</i></p>

<h2>Description</h2>
<p>FFspider is a multi-threaded web crawler and data extraction tool designed for iterative site discovery. It is an all-in-one file that allows users to initiate site crawling starting from a provided URL. Please note that this crawler is relatively simple and does not meet the criteria of a "polite crawler" as it does not handle for instance load management on the target sites.</p>
<p>Initially created for internal needs, FFspider was developed to assist in the creation of image databases for AI research projects and the creation and the relevance evaluation of CNN models.</p>

<h2>Features</h2>
<ul>
  <li>Multi-threaded crawling for improved performance.</li>
  <li>Flexible data extraction using CSS selectors and XPath.</li>
  <li><strong>TODO:</strong> Support for handling JavaScript-rendered pages using headless browsers.</li>
  <li>Extensible architecture for adding custom data processing and storage options.</li>
  <li>In-memory object database system to maximize performance during the crawling and processing of images.</li>
  <li>automatically image storing during the crawling process to a local cache directory for future reuse.</li>
  <li>Configurable options for controlling crawling behavior.</li>
</ul>
